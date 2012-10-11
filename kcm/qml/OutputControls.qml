/*
    Copyright (C) 2012  Dan Vratil <dvratil@redhat.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

import QtQuick 1.1
import org.kde.qtextracomponents 0.1
import org.kde.plasma.components 0.1 as PlasmaComponents;
import org.kde.plasma.core 0.1 as PlasmaCore
import KScreen 1.0

Item {
	id: root;
	property int rotationDirection;
	property Item parentItem;
	property int iconSize: 22;
	property int fontSize: 12;

	onWidthChanged: {
		adaptToSizeChange();
	}

	onHeightChanged: {
		adaptToSizeChange();
	}

	function adaptToSizeChange()
	{
		if ((width < 100) || (height < 80)) {
		      monitorName.visible = false;
		      outputNameAndSize.anchors.top = root.top;
		      enabledButton.scale = 0.4;
		      enabledButton.anchors.topMargin = 0;
		      root.fontSize = 10;
		      root.iconSize = 12;
		} else if ((width < 120) || (height < 100)) {
		      monitorName.visible = true;
		      outputNameAndSize.anchors.top = monitorName.bottom;
		      enabledButton.scale = 0.6;
		      enabledButton.anchors.topMargin = 2;
		      root.fontSize = 10;
		      root.iconSize = 16;
		} else {
		      monitorName.visible = true;
		      outputNameAndSize.anchors.top = monitorName.bottom;
		      enabledButton.scale = 0.8
		      enabledButton.anchors.topMargin = 4;
		      root.fontSize = 12;
		      root.iconSize = 22;
		}
	}

	Behavior on rotation {
		RotationAnimation {
			easing.type: "OutCubic"
			duration: 250;
			direction: (rotationDirection == RotationAnimation.Clockwise) ?
				RotationAnimation.Counterclockwise : RotationAnimation.Clockwise;
		}
	}

	Behavior on width {
	      PropertyAnimation {
		      duration: 250;
		      easing.type: "OutCubic";
	      }
	}

	Behavior on height {
	      PropertyAnimation {
		      duration: 250;
		      easing.type: "OutCubic";
	      }
	}


	state: "normal";
	states: [
		State {
			name: "normal";
			when: output.rotation == Output.None;
			PropertyChanges {
				target: root;
				rotation: 0;
				width: parent.width - 36;
				height: parent.height - 20;
			}
		},
		State {
			name: "left";
			when: output.rotation == Output.Left;
			PropertyChanges {
				target: root;
				rotation: 270;
				width: parent.height - 20;
				height: parent.width - 36;
			}
		},
		State {
			name: "inverted";
			when: output.rotation == Output.Inverted;
			PropertyChanges {
				target: root;
				rotation: 180;
				width: parent.width - 36;
				height: parent.height - 20;
			}
		},
		State {
			name: "right";
			when: output.rotation == Output.Right;
			PropertyChanges {
				target: root;
				rotation: 90;
				width: parent.height - 20;
				height: parent.width - 36;
			}
		}
	]

	/* Output name */
	Text {
		id: monitorName;
		text: output.edid.vendor;
		color: "white";
		font.family: theme.desktopFont.family;
		font.pointSize: root.fontSize;
		elide: Text.ElideRight;

		anchors {
			top: root.top;
			topMargin: 5;
		}
		width: parent.width;

		horizontalAlignment: Text.AlignHCenter;

		Behavior on font.pointSize {
			PropertyAnimation {
				duration: 100;
			}
		}
	}

	Text {
	      id: outputNameAndSize;
	      text: output.name;
	      color: "white";
	      font.family: theme.desktopFont.family;
	      font.pointSize: root.fontSize - 2;
	      width: parent.width;

	      anchors {
		      top: monitorName.bottom;
		      horizontalCenter: parent.horizontalCenter;
	      }

	      horizontalAlignment: Text.AlignHCenter;
	}

	/* Enable/Disable output */
	PlasmaComponents.Switch {
		id: enabledButton;


		anchors {
			top: outputNameAndSize.bottom;
			topMargin: 4;
			horizontalCenter: parent.horizontalCenter;
		}

		scale: 0.8;

		checked: output.enabled;
		onCheckedChanged: {
			/* FIXME: This should be in KScreen */
			if (output.enabled != enabledButton.checked) {
			  output.enabled = enabledButton.checked;
			}
		}
	}


	Row {

		anchors {
			horizontalCenter: parent.horizontalCenter;
			bottom: parent.bottom;
			bottomMargin: 5;
		}

		spacing: 5;
		visible: output.enabled;


		/* Rotation */
		IconButton {
			id: rotateButton;
			iconSize: root.iconSize;
			enabledIcon: "object-rotate-left";

			acceptedButtons: Qt.LeftButton | Qt.RightButton;
			onClicked: {
				if (mouse.button == Qt.LeftButton) {
					monitor.rotationDirection = RotationAnimation.Counterclockwise;
					if (output.rotation == Output.None) {
						output.rotation = Output.Right;
					} else if (output.rotation == Output.Right) {
						output.rotation = Output.Inverted;
					} else if (output.rotation == Output.Inverted) {
						output.rotation = Output.Left;
					} else if (output.rotation == Output.Left) {
						output.rotation = Output.None;
					}
				} else {
					monitor.rotationDirection = RotationAnimation.Clockwise;
					if (output.rotation == Output.None) {
						output.rotation = Output.Left;
					} else if (output.rotation == Output.Left) {
						output.rotation = Output.Inverted;
					} else if (output.rotation == Output.Inverted) {
						output.rotation = Output.Right;
					} else if (output.rotation == Output.Right) {
						output.rotation = Output.None;
					}
				}
			}
		}

		/* Primary toggle */
		IconButton {
			id: primaryButton;
			iconSize: root.iconSize;
			enabledIcon: "bookmarks";
			enabled: (output.enabled && output.primary);

			onClicked: {
				if (output.enabled) {
					output.primary = !output.primary
				}
			}
		}


		IconButton {
			id: resizeButton;
			iconSize: root.iconSize;
			enabledIcon: "view-restore"

			onClicked: selectionDialog.open();
		}
	}


	ModeSelectionDialog {
		id: selectionDialog;
		parentItem: parent.parentItem;
		visualParent: resizeButton;
	}
}