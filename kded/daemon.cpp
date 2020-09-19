/*
    SPDX-FileCopyrightText: 2012 Alejandro Fiestas Olivares <afiestas@kde.org>
    SPDX-FileCopyrightText: 2016 Sebastian Kügler <sebas@kde.org>
    SPDX-FileCopyrightText: 2018 Kai Uwe Broulik <kde@broulik.de>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "daemon.h"

#include "../common/orientation_sensor.h"
#include "config.h"
#include "generator.h"
#include "kdisplay_daemon_debug.h"
#include "kdisplayadaptor.h"
#include "osdmanager.h"

#include <disman/configmonitor.h>
#include <disman/getconfigoperation.h>
#include <disman/log.h>
#include <disman/output.h>
#include <disman/setconfigoperation.h>

#include <KActionCollection>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KPluginFactory>

#include <QAction>
#include <QOrientationReading>

K_PLUGIN_CLASS_WITH_JSON(KDisplayDaemon, "kdisplayd.json")

KDisplayDaemon::KDisplayDaemon(QObject* parent, const QList<QVariant>&)
    : KDEDModule(parent)
    , m_monitoring(true)
    , m_orientationSensor(new OrientationSensor(this))
{
    connect(m_orientationSensor,
            &OrientationSensor::availableChanged,
            this,
            &KDisplayDaemon::updateOrientation);
    connect(m_orientationSensor,
            &OrientationSensor::valueChanged,
            this,
            &KDisplayDaemon::updateOrientation);

    Disman::Log::instance();
    QMetaObject::invokeMethod(this, "getInitialConfig", Qt::QueuedConnection);
}

void KDisplayDaemon::getInitialConfig()
{
    connect(new Disman::GetConfigOperation,
            &Disman::GetConfigOperation::finished,
            this,
            [this](Disman::ConfigOperation* op) {
                if (op->has_error()) {
                    return;
                }

                m_monitoredConfig = qobject_cast<Disman::GetConfigOperation*>(op)->config();
                qCDebug(KDISPLAY_KDED) << "Config" << m_monitoredConfig.get() << "is ready";
                Disman::ConfigMonitor::instance()->add_config(m_monitoredConfig);

                init();
            });
}

void KDisplayDaemon::init()
{
    KActionCollection* coll = new KActionCollection(this);
    QAction* action = coll->addAction(QStringLiteral("display"));
    action->setText(i18n("Switch Display"));
    QList<QKeySequence> switchDisplayShortcuts({Qt::Key_Display, Qt::MetaModifier + Qt::Key_P});
    KGlobalAccel::self()->setGlobalShortcut(action, switchDisplayShortcuts);
    connect(action, &QAction::triggered, this, &KDisplayDaemon::displayButton);

    new KdisplayAdaptor(this);
    // Initialize OSD manager to register its dbus interface
    m_osdManager = new Disman::OsdManager(this);

    monitorConnectedChange();

    applyConfig();
    m_startingUp = false;
}

void KDisplayDaemon::updateOrientation()
{
    if (!m_monitoredConfig) {
        return;
    }
    const auto features = m_monitoredConfig->supported_features();
    if (!features.testFlag(Disman::Config::Feature::AutoRotation)
        || !features.testFlag(Disman::Config::Feature::TabletMode)) {
        return;
    }

    if (!m_orientationSensor->available() || !m_orientationSensor->enabled()) {
        return;
    }

    const auto orientation = m_orientationSensor->value();
    if (orientation == QOrientationReading::Undefined) {
        // Orientation sensor went off. Do not change current orientation.
        return;
    }
    if (orientation == QOrientationReading::FaceUp
        || orientation == QOrientationReading::FaceDown) {
        // We currently don't do anything with FaceUp/FaceDown, but in the future we could use them
        // to shut off and switch on again a display when display is facing downwards/upwards.
        return;
    }

    Config(m_monitoredConfig).setDeviceOrientation(orientation);
    if (m_monitoring) {
        doApplyConfig(m_monitoredConfig);
    } else {
        m_configDirty = true;
    }
}

void KDisplayDaemon::doApplyConfig(Disman::ConfigPtr const& config)
{
    qCDebug(KDISPLAY_KDED) << "Do set and apply specific config";

    qCWarning(KDISPLAY_KDED)
        << "Currently all KDisplay daemon config control is disabled. Doing nothing";
    return;

    m_monitoredConfig = config;
    refreshConfig();
}

void KDisplayDaemon::refreshConfig()
{
    setMonitorForChanges(false);
    m_configDirty = false;
    Disman::ConfigMonitor::instance()->add_config(m_monitoredConfig);

    connect(new Disman::SetConfigOperation(m_monitoredConfig),
            &Disman::SetConfigOperation::finished,
            this,
            [this]() {
                qCDebug(KDISPLAY_KDED) << "Config applied";
                if (m_configDirty) {
                    // Config changed in the meantime again, apply.
                    doApplyConfig(m_monitoredConfig);
                } else {
                    setMonitorForChanges(true);
                }
            });
}

void KDisplayDaemon::applyConfig()
{
    qCDebug(KDISPLAY_KDED) << "Applying config";

    const bool showOsd = m_monitoredConfig->outputs().size() > 1 && !m_startingUp
        && m_monitoredConfig->cause() == Disman::Config::Cause::generated;

    if (showOsd) {
        qCDebug(KDISPLAY_KDED) << "Getting ideal config from user via OSD...";
        auto action = m_osdManager->showActionSelector();
        connect(action, &Disman::OsdAction::selected, this, &KDisplayDaemon::applyOsdAction);
    } else {
        m_osdManager->hideOsd();
    }

    m_orientationSensor->setEnabled(Config(m_monitoredConfig).autoRotationRequested());
    updateOrientation();
}

void KDisplayDaemon::applyLayoutPreset(const QString& presetName)
{
    const QMetaEnum actionEnum = QMetaEnum::fromType<Disman::OsdAction::Action>();
    Q_ASSERT(actionEnum.isValid());

    bool ok;
    auto action = static_cast<Disman::OsdAction::Action>(
        actionEnum.keyToValue(qPrintable(presetName), &ok));
    if (!ok) {
        qCWarning(KDISPLAY_KDED) << "Cannot apply unknown screen layout preset named" << presetName;
        return;
    }
    applyOsdAction(action);
}

bool KDisplayDaemon::getAutoRotate()
{
    return Config(m_monitoredConfig).getAutoRotate();
}

void KDisplayDaemon::setAutoRotate(bool value)
{
    if (!m_monitoredConfig) {
        return;
    }
    Config(m_monitoredConfig).setAutoRotate(value);
    m_orientationSensor->setEnabled(value);
}

void KDisplayDaemon::applyOsdAction(Disman::OsdAction::Action action)
{
    Disman::ConfigPtr config;

    switch (action) {
    case Disman::OsdAction::NoAction:
        qCDebug(KDISPLAY_KDED) << "OSD: no action";
        break;
    case Disman::OsdAction::SwitchToInternal:
        qCDebug(KDISPLAY_KDED) << "OSD: switch to internal";
        config = Generator::displaySwitch(Generator::Action::TurnOffExternal, m_monitoredConfig);
        break;
    case Disman::OsdAction::SwitchToExternal:
        qCDebug(KDISPLAY_KDED) << "OSD: switch to external";
        config = Generator::displaySwitch(Generator::Action::TurnOffEmbedded, m_monitoredConfig);
        break;
    case Disman::OsdAction::ExtendLeft:
        qCDebug(KDISPLAY_KDED) << "OSD: extend left";
        config = Generator::displaySwitch(Generator::Action::ExtendToLeft, m_monitoredConfig);
        break;
    case Disman::OsdAction::ExtendRight:
        qCDebug(KDISPLAY_KDED) << "OSD: extend right";
        config = Generator::displaySwitch(Generator::Action::ExtendToRight, m_monitoredConfig);
        return;
    case Disman::OsdAction::Clone:
        qCDebug(KDISPLAY_KDED) << "OSD: clone";
        config = Generator::displaySwitch(Generator::Action::Clone, m_monitoredConfig);
        break;
    }
    if (config) {
        doApplyConfig(config);
    }
}

void KDisplayDaemon::configChanged()
{
    qCDebug(KDISPLAY_KDED) << "Change detected" << m_monitoredConfig;
    updateOrientation();
}

void KDisplayDaemon::showOsd(const QString& icon, const QString& text)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String("org.kde.plasmashell"),
                                                      QLatin1String("/org/kde/osdService"),
                                                      QLatin1String("org.kde.osdService"),
                                                      QLatin1String("showText"));
    msg << icon << text;
    QDBusConnection::sessionBus().asyncCall(msg);
}

void KDisplayDaemon::showOutputIdentifier()
{
    m_osdManager->showOutputIdentifiers();
}

void KDisplayDaemon::displayButton()
{
    qCDebug(KDISPLAY_KDED) << "displayBtn triggered";

    auto action = m_osdManager->showActionSelector();
    connect(action, &Disman::OsdAction::selected, this, &KDisplayDaemon::applyOsdAction);
}

void KDisplayDaemon::monitorConnectedChange()
{
    auto cfg = m_monitoredConfig.get();

    connect(cfg, &Disman::Config::output_added, this, &KDisplayDaemon::applyConfig);
    connect(cfg, &Disman::Config::output_removed, this, &KDisplayDaemon::applyConfig);
}

void KDisplayDaemon::setMonitorForChanges(bool enabled)
{
    if (m_monitoring == enabled) {
        return;
    }

    qCDebug(KDISPLAY_KDED) << "Monitor for changes: " << enabled;
    m_monitoring = enabled;

    if (m_monitoring) {
        connect(Disman::ConfigMonitor::instance(),
                &Disman::ConfigMonitor::configuration_changed,
                this,
                &KDisplayDaemon::configChanged,
                Qt::UniqueConnection);
    } else {
        disconnect(Disman::ConfigMonitor::instance(),
                   &Disman::ConfigMonitor::configuration_changed,
                   this,
                   &KDisplayDaemon::configChanged);
    }
}

#include "daemon.moc"
