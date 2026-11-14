import QtQuick 2.15

Item {
    id: field

    property bool active: false
    property real u: 1
    property color plateColor: "#ffffff"
    property color inkColor: "#000000"
    property bool caretOn: true

    property alias text: input.text

    signal activated()
    signal accepted()
    signal edited()
    signal escapePressed()

    function takeFocus() { input.forceActiveFocus() }
    function clear() { input.text = "" }
    function setText(t) { input.text = t }

    onActiveChanged: caretOn = true

    Rectangle {
        id: plate
        anchors.fill: parent
        color: field.plateColor
        border.width: 2 * field.u
        border.color: field.inkColor
    }

    Item {
        id: inner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 26 * field.u
        anchors.rightMargin: 26 * field.u
        height: 24 * field.u
        clip: true

        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12 * field.u
            x: Math.min(0, inner.width - row.width)

            Repeater {
                model: input.text.length
                delegate: Rectangle {
                    id: square
                    width: 24 * field.u
                    height: 24 * field.u
                    color: field.inkColor
                    opacity: index === input.text.length - 1 ? 0 : 1
                    Behavior on opacity {
                        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                    }

                    Component.onCompleted: {
                        if (index === input.text.length - 1) revealTimer.start()
                    }

                    Timer {
                        id: revealTimer
                        interval: 0
                        onTriggered: square.opacity = 1
                    }
                }
            }

            Rectangle {
                id: caret
                visible: field.active
                width: 24 * field.u
                height: 24 * field.u
                color: "transparent"
                border.width: 2 * field.u
                border.color: field.inkColor
                opacity: field.caretOn ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 80 } }
            }
        }
    }

    TextInput {
        id: input
        anchors.fill: parent
        color: "transparent"
        selectionColor: "transparent"
        selectedTextColor: "transparent"
        echoMode: TextInput.NoEcho
        cursorVisible: false
        selectByMouse: false
        focus: true
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 20 * field.u

        onTextEdited: {
            if (!field.active) field.activated()
            field.caretOn = true
            field.edited()
        }
        onAccepted: {
            if (input.text.length > 0) field.accepted()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                event.accepted = true
                field.escapePressed()
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.IBeamCursor
        onClicked: {
            if (!field.active) field.activated()
            input.forceActiveFocus()
        }
    }

    Timer {
        interval: 600
        repeat: true
        running: field.active
        onTriggered: field.caretOn = !field.caretOn
    }
}
