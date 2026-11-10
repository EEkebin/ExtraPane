#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <stdexcept>
#include <iostream>
class Receiver : public QQmlPropertyMap {
    Q_OBJECT
public:
    Receiver() : QQmlPropertyMap(this, nullptr) {
        insert("active", true);
        insert("status", "Connected");
        insert("sourceWidth", 1920);
        insert("sourceHeight", 1080);
        insert("statistics", "60 target FPS");
        insert("decoder", "Fake decoder");
    }
    int disconnects = 0, releases = 0;
    Q_INVOKABLE void disconnectFromServer() { ++disconnects; }
    Q_INVOKABLE void releaseInput() { ++releases; }
};
void require(bool result) {
    if (!result) throw std::runtime_error("Linux window assertion failed");
}
QObject* find(QObject* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (auto* child : root->children())
        if (auto* result = find(child, name)) return result;
    return nullptr;
}
int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QQuickStyle::setStyle("Material");
    QGuiApplication app(argc, argv);
    try {
        Receiver receiver;
        QQmlApplicationEngine engine;
        bool warned = false;
        QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&] { warned = true; });
        engine.rootContext()->setContextProperty("receiver", &receiver);
        engine.load(QUrl("qrc:/ReceiverWindow.qml"));
        require(!engine.rootObjects().isEmpty());
        auto* root = engine.rootObjects().first();
        auto* window = qobject_cast<QQuickWindow*>(root);
        require(window);
        app.processEvents();
        auto* disconnect = find(root, "displayDisconnect");
        require(disconnect && QMetaObject::invokeMethod(disconnect, "clicked"));
        require(receiver.disconnects == 1);
        require(QMetaObject::invokeMethod(root, "toggleFullscreen"));
        app.processEvents();
        require(receiver.releases > 0);
        window->close();
        app.processEvents();
        require(receiver.disconnects == 2 && window->isVisible());
        window->resize(390, 700);
        app.processEvents();
        require(!warned);
        std::cout << "Linux receiver window isolated controls passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#include "window_check.moc"
