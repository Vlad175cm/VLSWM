#include <QGuiApplication>
#include <QQuickView>
#include <QQmlContext>
#include <QFile>
#include <QProcess>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <QDebug>

#include <security/pam_appl.h>
#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

static QString current_user() {
    if (struct passwd *pw = getpwuid(getuid())) {
        return QString::fromLocal8Bit(pw->pw_name);
    }
    return qEnvironmentVariable("USER");
}

static int pam_conversation(int count, const struct pam_message **messages,
                            struct pam_response **responses, void *data) {
    const QByteArray *password = static_cast<const QByteArray *>(data);
    auto *reply = static_cast<struct pam_response *>(
        std::calloc((size_t)count, sizeof(struct pam_response)));
    if (!reply) return PAM_BUF_ERR;
    for (int i = 0; i < count; i++) {
        if (messages[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
            messages[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            reply[i].resp = strdup(password->constData());
        }
    }
    *responses = reply;
    return PAM_SUCCESS;
}

static bool pam_authenticate_user(const QString &user, const QString &password) {
    QByteArray pass = password.toUtf8();
    struct pam_conv conv = {pam_conversation, &pass};
    pam_handle_t *handle = nullptr;
    QByteArray name = user.toUtf8();
    // The installer creates /etc/pam.d/vlswm-lock. If it is missing (for
    // example after a partial update), system-auth is the same PAM stack a
    // normal login uses, so the lock screen still accepts the password.
    const char *service = access("/etc/pam.d/vlswm-lock", R_OK) == 0
        ? "vlswm-lock" : "system-auth";
    int result = pam_start(service, name.constData(), &conv, &handle);
    if (result == PAM_SUCCESS) result = pam_authenticate(handle, 0);
    if (result == PAM_SUCCESS) result = pam_acct_mgmt(handle, 0);
    if (handle) pam_end(handle, result);
    return result == PAM_SUCCESS;
}

class SddmShim : public QObject {
    Q_OBJECT
  public:
    explicit SddmShim(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void login(const QString &user, const QString &password, int session) {
        Q_UNUSED(session);
        QString name = user.isEmpty() ? current_user() : user;
        if (pam_authenticate_user(name, password)) {
            QCoreApplication::quit();
        } else {
            emit loginFailed();
        }
    }

    Q_INVOKABLE void powerOff() { run_systemctl("poweroff"); }
    Q_INVOKABLE void reboot() { run_systemctl("reboot"); }
    Q_INVOKABLE void suspend() { run_systemctl("suspend"); }

  signals:
    void loginFailed();

  private:
    static void run_systemctl(const QString &action) {
        QProcess::startDetached(QStringLiteral("systemctl"), {action});
    }
};

class UserModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastUser READ lastUser CONSTANT)
  public:
    explicit UserModel(QObject *parent = nullptr) : QObject(parent) {}
    QString lastUser() const { return current_user(); }
};

class SessionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int lastIndex READ lastIndex CONSTANT)
  public:
    explicit SessionModel(QObject *parent = nullptr) : QObject(parent) {}
    int lastIndex() const { return 0; }
};

static QVariantMap read_theme_config(const QString &path) {
    QVariantMap values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return values;
    QTextStream in(&file);
    QString section;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2).trimmed();
            continue;
        }
        int eq = line.indexOf('=');
        if (eq <= 0 || section != QStringLiteral("General")) continue;
        QString key = line.left(eq).trimmed();
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.mid(1, value.size() - 2);
        }
        values.insert(key, value);
    }
    return values;
}

int main(int argc, char **argv) {
    // This is a Wayland client. Without an explicit platform Qt may choose
    // xcb just because DISPLAY is set, and in Xwayland the lock window is
    // owned by another PID, so the compositor would not recognise it.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "wayland");
    }
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("vlswm-lock"));
    QGuiApplication::setDesktopFileName(QStringLiteral("vlswm-lock"));

    if (access("/etc/pam.d/vlswm-lock", R_OK) != 0) {
        qWarning("vlswm-lock: /etc/pam.d/vlswm-lock is missing, using system-auth");
    }

    QString theme_dir = QStringLiteral("/usr/share/sddm/themes/vlswm");
    QString qml = theme_dir + QStringLiteral("/Main.qml");
    if (!QFile::exists(qml)) {
        theme_dir = QStringLiteral("/usr/share/vlswm/lock");
        qml = theme_dir + QStringLiteral("/Main.qml");
    }
    if (!QFile::exists(qml)) {
        theme_dir = QCoreApplication::applicationDirPath() + QStringLiteral("/../lock");
        qml = theme_dir + QStringLiteral("/Main.qml");
    }
    if (!QFile::exists(qml)) {
        qWarning("vlswm-lock: Main.qml not found");
        return 1;
    }

    QVariantMap config = read_theme_config(theme_dir + QStringLiteral("/theme.conf"));
    if (!config.contains(QStringLiteral("palette"))) config.insert(QStringLiteral("palette"), QStringLiteral("dark"));
    if (!config.contains(QStringLiteral("title"))) config.insert(QStringLiteral("title"), QStringLiteral("VLSWM"));
    config.insert(QStringLiteral("demo"), QStringLiteral("false"));

    SddmShim sddm;
    UserModel user_model;
    SessionModel session_model;

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setColor(Qt::black);
    view.rootContext()->setContextProperty(QStringLiteral("config"), config);
    view.rootContext()->setContextProperty(QStringLiteral("sddm"), &sddm);
    view.rootContext()->setContextProperty(QStringLiteral("userModel"), &user_model);
    view.rootContext()->setContextProperty(QStringLiteral("sessionModel"), &session_model);
    view.setSource(QUrl::fromLocalFile(qml));
    if (view.status() == QQuickView::Error) {
        for (const auto &error : view.errors()) qWarning() << error.toString();
        return 1;
    }
    view.setTitle(QStringLiteral("vlswm-lock"));
    view.showFullScreen();
    return app.exec();
}

#include "vlswm-lock.moc"
