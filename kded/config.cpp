/********************************************************************
Copyright 2012 Alejandro Fiestas Olivares <afiestas@kde.org>
Copyright 2019 Roman Gilg <subdiff@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "config.h"
#include "output.h"
#include "../common/control.h"
#include "kdisplay_daemon_debug.h"
#include "device.h"

#include <QFile>
#include <QStandardPaths>
#include <QRect>
#include <QJsonDocument>
#include <QDir>

#include <disman/config.h>
#include <disman/output.h>

QString Config::s_fixedConfigFileName = QStringLiteral("fixed-config");
QString Config::s_configsDirName = QStringLiteral("" /*"configs/"*/); // TODO: KDE6 - move these files into the subfolder

QString Config::configsDirPath()
{
    return Globals::dirPath() % s_configsDirName;
}

Config::Config(Disman::ConfigPtr config, QObject *parent)
    : QObject(parent)
    , m_data(config)
    , m_control(new ControlConfig(config, this))
{
}

QString Config::filePath()
{
    if (!QDir().mkpath(configsDirPath())) {
        return QString();
    }
    return configsDirPath() % id();
}

QString Config::id() const
{
    if (!m_data) {
        return QString();
    }
    return m_data->connectedOutputsHash();
}

void Config::activateControlWatching()
{
    connect(m_control, &ControlConfig::changed, this, &Config::controlChanged);
    m_control->activateWatcher();
}

bool Config::autoRotationRequested() const
{
    for (Disman::OutputPtr &output : m_data->outputs()) {
        if (m_control->getAutoRotate(output)) {
            return true;
        }
    }
    return false;
}

void Config::setDeviceOrientation(QOrientationReading::Orientation orientation)
{
    for (Disman::OutputPtr &output : m_data->outputs()) {
        if (!m_control->getAutoRotate(output)) {
            continue;
        }
        auto finalOrientation = orientation;
        if (m_control->getAutoRotateOnlyInTabletMode(output) && !m_data->tabletModeEngaged()) {
            finalOrientation = QOrientationReading::Orientation::TopUp;
        }
        if (Output::updateOrientation(output, finalOrientation)) {
            // TODO: call Layouter to find fitting positions for other outputs again
            return;
        }
    }
}

bool Config::getAutoRotate() const
{
    const auto outputs = m_data->outputs();
    return std::all_of(outputs.cbegin(), outputs.cend(),
        [this](Disman::OutputPtr output) {
            if (output->type() != Disman::Output::Type::Panel) {
                return true;
            }
            return m_control->getAutoRotate(output);
        });
}

void Config::setAutoRotate(bool value)
{
    for (Disman::OutputPtr &output : m_data->outputs()) {
        if (output->type() != Disman::Output::Type::Panel) {
            continue;
        }
        if (m_control->getAutoRotate(output) != value) {
            m_control->setAutoRotate(output, value);
        }
    }
    m_control->writeFile();
}

bool Config::fileExists() const
{
    return (QFile::exists(configsDirPath() % id()) || QFile::exists(configsDirPath() % s_fixedConfigFileName));
}

std::unique_ptr<Config> Config::readFile()
{
    if (Device::self()->isLaptop() && !Device::self()->isLidClosed()) {
        // We may look for a config that has been set when the lid was closed, Bug: 353029
        const QString lidOpenedFilePath(filePath() % QStringLiteral("_lidOpened"));
        const QFile srcFile(lidOpenedFilePath);

        if (srcFile.exists()) {
            QFile::remove(filePath());
            if (QFile::copy(lidOpenedFilePath, filePath())) {
                QFile::remove(lidOpenedFilePath);
                qCDebug(KDISPLAY_KDED) << "Restored lid opened config to" << id();
            }
        }
    }
    return readFile(id());
}

std::unique_ptr<Config> Config::readOpenLidFile()
{
    const QString openLidFile = id() % QStringLiteral("_lidOpened");
    auto config = readFile(openLidFile);
    QFile::remove(configsDirPath() % openLidFile);
    return config;
}

std::unique_ptr<Config> Config::readFile(const QString &fileName)
{
    if (!m_data) {
        return nullptr;
    }
    auto config = std::unique_ptr<Config>(new Config(m_data->clone()));
    config->setValidityFlags(m_validityFlags);

    QFile file;
    if (QFile::exists(configsDirPath() % s_fixedConfigFileName)) {
        file.setFileName(configsDirPath() % s_fixedConfigFileName);
        qCDebug(KDISPLAY_KDED) << "found a fixed config, will use " << file.fileName();
    } else {
        file.setFileName(configsDirPath() % fileName);
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qCDebug(KDISPLAY_KDED) << "failed to open file" << file.fileName();
        return nullptr;
    }

    QJsonDocument parser;
    QVariantList outputs = parser.fromJson(file.readAll()).toVariant().toList();
    Output::readInOutputs(config->data(), outputs);

    QSize screenSize;
    for (const auto &output : config->data()->outputs()) {
        if (!output->isPositionable()) {
            continue;
        }

        auto const geom = output->geometry();
        if (geom.x() + geom.width() > screenSize.width()) {
            screenSize.setWidth(geom.x() + geom.width());
        }
        if (geom.y() + geom.height() > screenSize.height()) {
            screenSize.setHeight(geom.y() + geom.height());
        }
    }
    config->data()->screen()->setCurrentSize(screenSize);

    if (!canBeApplied(config->data())) {
        return nullptr;
    }
    return config;
}

bool Config::canBeApplied() const
{
    return canBeApplied(m_data);
}

bool Config::canBeApplied(Disman::ConfigPtr config) const
{
#ifdef KDED_UNIT_TEST
    Q_UNUSED(config);
    return true;
#else
    return Disman::Config::canBeApplied(config, m_validityFlags);
#endif
}

bool Config::writeFile()
{
    return writeFile(filePath());
}

bool Config::writeOpenLidFile()
{
    return writeFile(filePath() % QStringLiteral("_lidOpened"));
}

bool Config::writeFile(const QString &filePath)
{
    if (id().isEmpty()) {
        return false;
    }
    const Disman::OutputList outputs = m_data->outputs();

    const auto oldConfig = readFile();
    Disman::OutputList oldOutputs;
    if (oldConfig) {
        oldOutputs = oldConfig->data()->outputs();
    }

    QVariantList outputList;
    for (const Disman::OutputPtr &output : outputs) {
        QVariantMap info;

        const auto oldOutputIt = std::find_if(oldOutputs.constBegin(), oldOutputs.constEnd(),
                                              [output](const Disman::OutputPtr &out) {
                                                  return out->hashMd5() == output->hashMd5();
                                               }
        );
        const Disman::OutputPtr oldOutput = oldOutputIt != oldOutputs.constEnd() ? *oldOutputIt :
                                                                                    nullptr;

        if (!output->isConnected()) {
            continue;
        }

        Output::writeGlobalPart(output, info, oldOutput);
        info[QStringLiteral("primary")] = output->isPrimary();
        info[QStringLiteral("enabled")] = output->isEnabled();

        auto setOutputConfigInfo = [&info](const Disman::OutputPtr &out) {
            if (!out) {
                return;
            }

            QVariantMap pos;
            pos[QStringLiteral("x")] = out->position().x();
            pos[QStringLiteral("y")] = out->position().y();
            info[QStringLiteral("pos")] = pos;
        };
        setOutputConfigInfo(output->isEnabled() ? output : oldOutput);

        if (output->isEnabled() &&
                m_control->getOutputRetention(output->hash(), output->name()) !=
                    Control::OutputRetention::Individual) {
            // try to update global output data
            Output::writeGlobal(output);
        }

        outputList.append(info);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(KDISPLAY_KDED) << "Failed to open config file for writing! " << file.errorString();
        return false;
    }
    file.write(QJsonDocument::fromVariant(outputList).toJson());
    qCDebug(KDISPLAY_KDED) << "Config saved on: " << file.fileName();

    return true;
}

void Config::log()
{
    if (!m_data) {
        return;
    }
    const auto outputs = m_data->outputs();
    for (const auto &o : outputs) {
        if (o->isConnected()) {
            qCDebug(KDISPLAY_KDED) << o;
        }
    }
}
