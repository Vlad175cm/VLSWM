import QtQuick 2.15
import QtQuick.Window 2.15

// Fallback lock screen used when the SDDM theme is not installed. It is the
// same UI as the theme, minus the SDDM-only user-list helper and demo timer:
// vlswm-lock always logs in the current user through userModel.lastUser.
Rectangle {
    id: root

    width: Screen.width
    height: Screen.height
    color: root.bg

    readonly property var cfg: (typeof config !== "undefined" && config) ? config : ({})
    readonly property string titleText: (cfg.title !== undefined && String(cfg.title).length > 0) ? String(cfg.title) : "VLSWM"

    property bool light: String(cfg.palette || "dark").toLowerCase() === "light"
    property bool flash: false
    property bool active: false
    property string errorText: ""

    // Palette. Theme switches are animated; the failure flash swaps the two
    // roles instantly, which is a true full-screen inversion without shaders.
    property color bgBase: light ? "#ffffff" : "#000000"
    property color fgBase: light ? "#000000" : "#ffffff"
    Behavior on bgBase { ColorAnimation { duration: 180; easing.type: Easing.InOutQuad } }
    Behavior on fgBase { ColorAnimation { duration: 180; easing.type: Easing.InOutQuad } }
    readonly property color bg: flash ? fgBase : bgBase
    readonly property color fg: flash ? bgBase : fgBase

    readonly property real u: Math.max(0.75, Math.min(width / 1440, height / 900))
    readonly property real titleSize: Math.min(width * 0.155, height * 0.245)
    readonly property real fieldWidth: Math.min(width * 0.32, 520 * u)
    readonly property real fieldHeight: 76 * u
    readonly property real fieldTop: height * 0.40

    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: root.activate()
    }

    Text {
        id: title
        text: root.titleText
        color: root.fg
        font.family: "JetBrainsMono Nerd Font"
        font.weight: Font.Bold
        font.pixelSize: root.titleSize
        font.letterSpacing: root.titleSize * 0.06
        renderType: Text.NativeRendering
        x: (root.width - width) / 2
        y: root.active ? 0 : (root.height - height) / 2
        Behavior on y { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
    }

    PasswordField {
        id: password
        width: root.fieldWidth
        height: root.fieldHeight
        u: root.u
        plateColor: root.fg
        inkColor: root.bg
        active: root.active
        x: (root.width - width) / 2
        y: root.fieldTop + (root.active ? 0 : 48 * root.u)
        opacity: root.active ? 1 : 0
        Behavior on y { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }

        onActivated: root.activate()
        onAccepted: root.doLogin()
        onEdited: root.errorText = ""
        onEscapePressed: root.onEscape()
    }

    Text {
        id: errorLabel
        text: root.errorText
        visible: text.length > 0
        color: root.fg
        font.family: "JetBrainsMono Nerd Font"
        font.weight: Font.Bold
        font.pixelSize: 15 * root.u
        font.letterSpacing: 4 * root.u
        renderType: Text.NativeRendering
        x: (root.width - width) / 2
        y: password.y + password.height + 22 * root.u
    }

    Timer {
        id: flashTimer
        interval: 80
        onTriggered: root.flash = false
    }

    Timer {
        interval: 200
        running: true
        onTriggered: password.takeFocus()
    }

    Shortcut {
        sequence: "T"
        enabled: !root.active
        onActivated: root.toggleLight()
    }
    Shortcut {
        sequence: "Ctrl+T"
        onActivated: root.toggleLight()
    }
    Shortcut {
        sequence: "Ctrl+Shift+P"
        onActivated: { if (typeof sddm !== "undefined") sddm.powerOff() }
    }
    Shortcut {
        sequence: "Ctrl+Shift+R"
        onActivated: { if (typeof sddm !== "undefined") sddm.reboot() }
    }
    Shortcut {
        sequence: "Ctrl+Shift+S"
        onActivated: { if (typeof sddm !== "undefined") sddm.suspend() }
    }

    Connections {
        target: (typeof sddm !== "undefined") ? sddm : null
        function onLoginFailed() { root.loginFailedVisual() }
    }

    function activate() {
        if (!active) active = true
        errorText = ""
        password.takeFocus()
    }

    function toggleLight() {
        light = !light
        password.takeFocus()
    }

    function onEscape() {
        if (password.text.length > 0) {
            password.clear()
            errorText = ""
        } else {
            active = false
        }
    }

    function userName() {
        if (typeof userModel === "undefined") return ""
        if (userModel.lastUser) return String(userModel.lastUser)
        return ""
    }

    function sessionIndex() {
        if (typeof sessionModel === "undefined") return 0
        var i = sessionModel.lastIndex
        return (i !== undefined && i >= 0) ? i : 0
    }

    function doLogin() {
        if (typeof sddm === "undefined") return
        if (password.text.length === 0) return
        sddm.login(userName(), password.text, sessionIndex())
    }

    function loginFailedVisual() {
        errorText = "ACCESS DENIED"
        password.clear()
        flash = true
        flashTimer.restart()
        password.takeFocus()
    }
}
