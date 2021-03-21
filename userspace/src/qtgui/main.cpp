#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "clientsource/coordinator.h"

int main(int argc, char *argv[])
{
    qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1"); // this app will probably run as root
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    qmlRegisterType<WebBridge>("WebBridge", 1, 0, "Bridge");
    engine.load(QUrl(QStringLiteral("qrc:/clientsource/main.qml")));
    Coordinator coord(WebBridge::lastInstance());

    return app.exec();
}
