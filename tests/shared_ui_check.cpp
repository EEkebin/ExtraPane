#include "resource_model.hpp"
#include "connection_model.hpp"
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QWindow>
#include <QQuickWindow>
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QQuickItem>
#include <QSet>
#include <QQuickStyle>
#include <QVariantList>
#include <QVariantMap>
#include <iostream>
#include <stdexcept>
#include <source_location>

using Kind = extrapane::ResourceKind;
static void require(bool condition, const std::source_location where = std::source_location::current()) {
    if (!condition) throw std::runtime_error("Shared UI model check failed at line " + std::to_string(where.line()));
}
static Kind kind(const QString& key) {
    if (key == "display") return Kind::display;
    if (key == "camera") return Kind::camera;
    if (key == "microphone") return Kind::microphone;
    if (key == "usb") return Kind::usb;
    throw std::runtime_error("Unexpected resource action");
}
class TestController final : public QQmlPropertyMap {
    Q_OBJECT
public:
    explicit TestController(ResourceModel& resources) : QQmlPropertyMap(this, nullptr), model(resources) {
        insert("resources", QVariant::fromValue(&model));
        connect(&model, &ResourceModel::selectionChanged, this, [this] { insert("clientReady", true); });
        insert("clientReady", true);
    }
    Q_INVOKABLE void releaseInput() {}
    Q_INVOKABLE void refreshDevices(QString key) {
        ++refreshRequests;
        refreshed = std::move(key);
    }
    Q_INVOKABLE void selectDevice(QString key, QString id) {
        ++deviceSelections;
        model.setSelectedDevice(kind(key), std::move(id));
    }
    Q_INVOKABLE void stopResource(QString key) {
        ++stopRequests;
        stopped = key;
        model.setSessionState(kind(key), false, "Stopped");
    }
    Q_INVOKABLE void setCameraMirrored(bool value) {
        ++mirrorRequests;
        insert("cameraMirrored", value);
    }
    Q_INVOKABLE void setMicrophoneMuted(bool value) {
        ++muteRequests;
        model.setSessionState(Kind::microphone, true, "Sharing", value);
    }
    Q_INVOKABLE void disconnectFromServer() {
        ++disconnectRequests;
        for (int i = 0; i < model.rowCount(); ++i) model.select(i, false);
    }
    Q_INVOKABLE void connectTo(QString, QString, bool, unsigned, unsigned, unsigned, QString) { ++connectRequests; }
    Q_INVOKABLE void setupUsbSource() { ++setupRequests; }
    Q_INVOKABLE void refreshUsbRecovery() { ++recoveryRefreshRequests; }
    Q_INVOKABLE void recoverUsb(QString id) {
        ++recoveryRequests;
        recoveredId = std::move(id);
    }
    Q_INVOKABLE void startHost(QString address, int port, bool code) {
        ++hostStartRequests;
        startedAddress = std::move(address);
        startedPort = port;
        startedRequireCode = code;
    }
    Q_INVOKABLE void stopHost() { ++hostStopRequests; }
    Q_INVOKABLE void ejectUsb() { ++ejectRequests; }
    ResourceModel& model;
    int refreshRequests = 0, disconnectRequests = 0, deviceSelections = 0, stopRequests = 0, muteRequests = 0,
        connectRequests = 0, mirrorRequests = 0;
    int setupRequests = 0, recoveryRefreshRequests = 0, recoveryRequests = 0, hostStartRequests = 0,
        hostStopRequests = 0, ejectRequests = 0;
    QString startedAddress, refreshed, stopped, recoveredId;
    int startedPort = 0;
    bool startedRequireCode = false;
};
class TestConnectionRuntime final : public QQmlPropertyMap {
    Q_OBJECT
public:
    explicit TestConnectionRuntime(ConnectionModel& connections) : QQmlPropertyMap(this, nullptr), model(connections) {}
    Q_INVOKABLE void toggle(QString id, bool enabled, QString code = {}, bool input = true) {
        Q_UNUSED(code);
        Q_UNUSED(input);
        requests.push_back(id);
        model.setEnabled(id, enabled);
        if (!enabled) model.setState(id, ConnectionModel::State::idle);
    }
    ConnectionModel& model;
    QStringList requests;
};
static QObject* named(QObject* root, const char* name) {
    const auto wanted = QString::fromLatin1(name);
    QSet<QObject*> seen;
    std::vector<QObject*> pending{root};
    while (!pending.empty()) {
        auto* object = pending.back();
        pending.pop_back();
        if (seen.contains(object)) continue;
        seen.insert(object);
        if (object->objectName() == wanted) return object;
        for (auto* child : object->children()) pending.push_back(child);
        if (auto* item = qobject_cast<QQuickItem*>(object))
            for (auto* child : item->childItems()) pending.push_back(child);
    }
    throw std::runtime_error(std::string("Missing UI control: ") + name);
}
static void defaults(TestController& controller, bool windows) {
    for (const auto* key : {"accessCode", "hostStatus", "savedAddress", "statistics", "decoder", "version",
                            "hostDescription", "inputDescription", "rendererDescription", "recoveryStatus"})
        controller.insert(key, QString{});
    controller.insert("platformLabel", QString(windows ? "Windows" : "Linux"));
    controller.insert("hostAvailability", QString(windows ? "" : "Headless check"));
    controller.insert("availability", QString(windows ? "" : "Headless check"));
    controller.insert("active", false);
    controller.insert("hosting", false);
    controller.insert("cameraMirrored", false);
    controller.insert("status", QString("Ready"));
    controller.insert("sourceWidth", 1280);
    controller.insert("sourceHeight", 720);
    controller.insert("addresses",
                      QVariantList{QVariantMap{{"label", "Automatic route"}, {"address", "0.0.0.0"}, {"index", 0}}});
    if (windows) {
        controller.insert("cameraSetupRunning", false);
        controller.insert("recoveryBusy", false);
        controller.insert("usbRecoveries", QVariantList{});
        controller.insert(
            "hostComponents",
            QVariantList{
                QVariantMap{{"label", "Display"}, {"available", true}, {"detail", "Driver checked on connection"}},
                QVariantMap{{"label", "Camera"}, {"available", false}, {"detail", "Camera setup required"}},
                QVariantMap{
                    {"label", "Microphone"}, {"available", false}, {"detail", "Receiving component setup required"}},
                QVariantMap{
                    {"label", "USB storage"}, {"available", false}, {"detail", "Receiving component setup required"}}});
    }
}
static QVariantList devices(const QString& id, const QString& label) {
    return {QVariantMap{{"label", label}, {"id", id}, {"available", true}}};
}
static void model_checks() {
    ResourceModel model;
    require(!model.canConnect());
    model.setCapability(Kind::display, {true, {}});
    require(model.canConnect());
    model.setCapability(Kind::camera, {true, {}});
    model.setCapability(Kind::microphone, {true, {}});
    model.setCapability(Kind::usb, {true, {}});
    model.select(1, true);
    model.select(2, true);
    require(model.selected(Kind::display) && model.selected(Kind::camera) && model.selected(Kind::microphone));
    require(!model.canConnect());
    model.setDevices(Kind::camera, devices("camera-a", "Camera"), {}, "Choose camera", false);
    require(model.selectedDevice(Kind::camera).isEmpty());
    model.setSelectedDevice(Kind::camera, "camera-a");
    require(!model.canConnect());
    model.setDevices(Kind::microphone, devices("mic-a", "Microphone"), {}, "Choose mic", false);
    model.setSelectedDevice(Kind::microphone, "mic-a");
    require(model.canConnect());
    model.setDevices(Kind::usb, devices("usb-a", "Storage"), {}, "Scanning unused resource", true);
    require(model.canConnect());
    model.setDevices(Kind::camera, model.devices(Kind::camera), "camera-a", "Scanning selected resource", true);
    require(!model.canConnect());
    model.setSelectedDevice(Kind::camera, {});
    require(model.selectedDevice(Kind::camera) == "camera-a");
    model.setDevices(Kind::camera, model.devices(Kind::camera), "camera-a", "Ready", false);
    require(model.canConnect());
    model.setSelectedDevice(Kind::camera, "missing");
    require(model.selectedDevice(Kind::camera) == "camera-a");
    model.setLocked(true);
    model.select(1, false);
    model.setSelectedDevice(Kind::microphone, {});
    require(model.selected(Kind::camera) && model.selectedDevice(Kind::microphone) == "mic-a" && !model.canConnect());
    model.setSessionState(Kind::camera, true, "Sharing camera");
    model.setSessionState(Kind::microphone, true, "Sharing microphone", true);
    model.setSessionState(Kind::camera, false, "Camera stopped");
    require(model.data(model.index(2), ResourceModel::Running).toBool() &&
            model.data(model.index(2), ResourceModel::Muted).toBool());
    model.setLocked(false);
    model.setDevices(Kind::camera, devices("camera-b", "Replacement"), "camera-a", "Camera changed", false);
    require(model.selectedDevice(Kind::camera).isEmpty() && model.selectedDevice(Kind::microphone) == "mic-a" &&
            !model.canConnect());
    model.setCapability(Kind::camera, {false, "Unavailable"});
    require(!model.selected(Kind::camera) && model.selected(Kind::microphone));
    require(model.canConnect());
    model.setCapability(Kind::display, {false, "Unavailable"});
    require(!model.selected(Kind::display) && model.canConnect());
    model.setDevices(Kind::microphone,
                     QVariantList{QVariantMap{{"label", "Blocked"}, {"id", "blocked"}, {"available", false}}}, {},
                     "Blocked", false);
    model.setSelectedDevice(Kind::microphone, "blocked");
    require(model.selectedDevice(Kind::microphone).isEmpty() && !model.canConnect());
    auto duplicate = devices("duplicate", "Mic one");
    duplicate += devices("duplicate", "Mic two");
    model.setDevices(Kind::microphone, duplicate, "duplicate", "Ambiguous", false);
    require(model.selectedDevice(Kind::microphone).isEmpty());
}
int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QQuickStyle::setStyle("Material");
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    require(QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/segoeui.ttf") >= 0);
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/seguisb.ttf");
#endif
    auto waitFor = [&](auto predicate) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < 2000) {
            app.processEvents();
            QThread::msleep(5);
        }
        require(predicate());
    };
    try {
        model_checks();
        bool warnings = false;
        auto watch = [&](QQmlApplicationEngine& engine) {
            QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError>& errors) {
                warnings = true;
                for (const auto& error : errors) std::cerr << error.toString().toStdString() << '\n';
            });
        };
        ResourceModel linuxModel;
        TestController linux(linuxModel);
        defaults(linux, false);
        QQmlApplicationEngine linuxEngine;
        watch(linuxEngine);
        linuxEngine.rootContext()->setContextProperty("receiver", &linux);
        linuxEngine.load(QUrl("qrc:/Main.qml"));
        require(!linuxEngine.rootObjects().isEmpty());
        app.processEvents();
        auto* linuxWindow = qobject_cast<QWindow*>(linuxEngine.rootObjects().first());
        require(linuxWindow);
        linuxWindow->resize(390, 844);
        app.processEvents();
        linuxWindow->resize(1120, 760);
        app.processEvents();
        require(!warnings);
        ResourceModel model;
        for (auto k : {Kind::display, Kind::camera, Kind::microphone, Kind::usb}) model.setCapability(k, {true, {}});
        TestController controller(model);
        defaults(controller, true);
        QQmlApplicationEngine engine;
        watch(engine);
        engine.rootContext()->setContextProperty("receiver", &controller);
        engine.load(QUrl("qrc:/Main.qml"));
        require(!engine.rootObjects().isEmpty());
        app.processEvents();
        require(controller.refreshRequests == 0 && controller.setupRequests == 0 && controller.hostStartRequests == 0 &&
                controller.recoveryRefreshRequests == 0 && controller.connectRequests == 0);
        auto* root = engine.rootObjects().first();
        root->setProperty("page", 0);
        app.processEvents();
        auto* server = named(root, "serverPage");
        auto* start = named(root, "serverStartButton");
        auto* address = named(root, "hostAddressPicker");
        require(named(root, "hostComponentsRepeater")->property("count").toInt() == 4);
        require(!named(root, "serverComponent_Display")->property("visible").toBool() &&
                named(root, "serverComponent_Camera")->property("visible").toBool());
        int serverCombos = 0;
        for (auto* object : server->findChildren<QObject*>())
            if (object->inherits("QQuickComboBox")) {
                ++serverCombos;
                require(object == address);
            }
        require(serverCombos == 1);
        for (auto* object : server->findChildren<QObject*>()) {
            const auto text = object->property("text").toString();
            require(text != "Camera setup" && text != "Receiving component setup");
        }
        require(start->property("enabled").toBool());
        require(QMetaObject::invokeMethod(start, "clicked"));
        require(controller.hostStartRequests == 1 && controller.startedAddress == "0.0.0.0" &&
                controller.startedPort == 43824 && !controller.startedRequireCode);
        controller.insert("hosting", true);
        controller.insert("hostAvailability", QString("Unavailable"));
        app.processEvents();
        require(start->property("enabled").toBool());
        require(start->property("background").value<QObject*>()->property("color").value<QColor>() ==
                QColor("#b32635"));
        require(QMetaObject::invokeMethod(start, "clicked"));
        require(controller.hostStopRequests == 1);
        controller.insert("hosting", false);
        app.processEvents();
        require(!start->property("enabled").toBool());
        controller.insert("hostAvailability", QString{});
        controller.insert("cameraSetupRunning", true);
        app.processEvents();
        require(!start->property("enabled").toBool());
        require(start->property("text").toString() == "Preparing server...");
        require(!address->property("enabled").toBool());
        controller.insert("cameraSetupRunning", false);
        root->setProperty("page", 1);
        app.processEvents();
        auto* picker = named(root, "resourcePicker");
        auto* connect = named(root, "connectButton");
        auto* disconnect = named(root, "clientDisconnectButton");
        auto* display = named(picker, "resourceCheck_display");
        auto* camera = named(picker, "resourceCheck_camera");
        auto* microphone = named(picker, "resourceCheck_microphone");
        auto* usb = named(picker, "resourceCheck_usb");
        require(display->property("checked").toBool() && camera->property("enabled").toBool() &&
                microphone->property("enabled").toBool() && usb->property("enabled").toBool());
        require(!named(root, "clientVideoLoader")->property("active").toBool());
        require(connect->property("enabled").toBool());
        // Check multiple resources through the actual checkbox signals; no fake
        // controller inventory, capture, setup or connection runs automatically.
        camera->setProperty("checked", true);
        require(QMetaObject::invokeMethod(camera, "clicked"));
        microphone->setProperty("checked", true);
        require(QMetaObject::invokeMethod(microphone, "clicked"));
        app.processEvents();
        require(model.selected(Kind::display) && model.selected(Kind::camera) && model.selected(Kind::microphone) &&
                connect->property("enabled").toBool());
        auto* mirror = named(picker, "resourceMirror_camera");
        require(mirror->property("visible").toBool() && mirror->property("enabled").toBool() &&
                !mirror->property("checked").toBool());
        mirror->setProperty("checked", true);
        require(QMetaObject::invokeMethod(mirror, "clicked"));
        app.processEvents();
        require(controller.mirrorRequests == 1 && controller.value("cameraMirrored").toBool() &&
                controller.connectRequests == 0);
        auto* cameraPicker = named(picker, "resourceDevicePicker_camera");
        auto* micPicker = named(picker, "resourceDevicePicker_microphone");
        require(cameraPicker->property("visible").toBool() && micPicker->property("visible").toBool());
        auto* cameraPopup = cameraPicker->property("popup").value<QObject*>();
        require(cameraPopup && cameraPicker->property("enabled").toBool() &&
                cameraPicker->property("count").toInt() == 0);
        require(QMetaObject::invokeMethod(cameraPopup, "open"));
        waitFor([&] { return cameraPopup->property("opened").toBool(); });
        require(controller.refreshed == "camera" && controller.refreshRequests == 1);
        require(QMetaObject::invokeMethod(cameraPopup, "close"));
        waitFor([&] { return !cameraPopup->property("visible").toBool(); });
        for (auto* object : picker->findChildren<QObject*>()) {
            require(!object->objectName().startsWith("resourceRefresh_"));
            require(object->property("text").toString() != "USB source setup");
        }
        model.setDevices(Kind::camera, devices("cam-a", "Built-in camera"), {}, "Choose camera", false);
        model.setDevices(Kind::microphone, devices("mic-a", "Built-in microphone"), {}, "Choose microphone", false);
        app.processEvents();
        require(cameraPicker->property("currentIndex").toInt() == -1 &&
                micPicker->property("currentIndex").toInt() == -1);
        cameraPicker->setProperty("currentIndex", 0);
        require(QMetaObject::invokeMethod(cameraPicker, "activated", Q_ARG(int, 0)));
        micPicker->setProperty("currentIndex", 0);
        require(QMetaObject::invokeMethod(micPicker, "activated", Q_ARG(int, 0)));
        app.processEvents();
        require(controller.deviceSelections == 2 && model.selectedDevice(Kind::camera) == "cam-a" &&
                model.selectedDevice(Kind::microphone) == "mic-a" && connect->property("enabled").toBool());
        model.setDevices(Kind::camera, model.devices(Kind::camera), "cam-a", "Listing devices...", true);
        app.processEvents();
        require(cameraPicker->property("enabled").toBool());
        require(QMetaObject::invokeMethod(cameraPopup, "open"));
        waitFor([&] { return cameraPopup->property("opened").toBool(); });
        require(controller.refreshRequests == 1);
        require(QMetaObject::invokeMethod(cameraPopup, "close"));
        waitFor([&] { return !cameraPopup->property("visible").toBool(); });
        model.setDevices(Kind::camera, model.devices(Kind::camera), "cam-a", "Ready", false);
        app.processEvents();
        require(QMetaObject::invokeMethod(cameraPopup, "open"));
        waitFor([&] { return cameraPopup->property("opened").toBool(); });
        require(controller.refreshRequests == 2 && model.selectedDevice(Kind::camera) == "cam-a");
        require(QMetaObject::invokeMethod(cameraPopup, "close"));
        waitFor([&] { return !cameraPopup->property("visible").toBool(); });
        require(QMetaObject::invokeMethod(connect, "clicked"));
        require(controller.connectRequests == 1);
        model.setLocked(false);
        controller.insert("active", true);
        model.setSessionState(Kind::display, true, "Receiving");
        model.setSessionState(Kind::camera, true, "Sharing");
        model.setSessionState(Kind::microphone, true, "Sharing");
        app.processEvents();
        require(picker->property("visible").toBool() && camera->property("enabled").toBool() &&
                !cameraPicker->property("enabled").toBool() && disconnect->property("visible").toBool());
        require(QMetaObject::invokeMethod(cameraPopup, "open"));
        waitFor([&] { return cameraPopup->property("opened").toBool(); });
        require(controller.refreshRequests == 2);
        require(QMetaObject::invokeMethod(cameraPopup, "close"));
        waitFor([&] { return !cameraPopup->property("visible").toBool(); });
        require(mirror->property("visible").toBool() && mirror->property("enabled").toBool());
        mirror->setProperty("checked", false);
        require(QMetaObject::invokeMethod(mirror, "clicked"));
        app.processEvents();
        require(controller.mirrorRequests == 2 && !controller.value("cameraMirrored").toBool() &&
                controller.connectRequests == 1 && controller.disconnectRequests == 0 && controller.stopRequests == 0 &&
                controller.muteRequests == 0);
        camera->setProperty("checked", false);
        require(QMetaObject::invokeMethod(camera, "clicked"));
        app.processEvents();
        require(!model.selected(Kind::camera) && model.selected(Kind::microphone) && model.selected(Kind::display));
        for (auto* object : picker->findChildren<QObject*>())
            require(!object->objectName().startsWith("resourceStop_"));

        require(QMetaObject::invokeMethod(named(picker, "resourceMute_microphone"), "clicked"));
        require(controller.muteRequests == 1 && model.data(model.index(2), ResourceModel::Muted).toBool());
        require(QMetaObject::invokeMethod(disconnect, "clicked"));
        require(controller.disconnectRequests == 1 && !model.selected(Kind::camera) && !model.selected(Kind::display) &&
                !model.selected(Kind::microphone));
        controller.insert("active", false);
        model.setLocked(false);
        model.setSessionState(Kind::display, false, "Stopped");
        model.setSessionState(Kind::microphone, false, "Stopped");
        usb->setProperty("checked", true);
        require(QMetaObject::invokeMethod(usb, "clicked"));
        app.processEvents();
        require(connect->property("enabled").toBool());
        model.setDevices(Kind::usb,
                         QVariantList{QVariantMap{{"label", "Protected disk"}, {"id", "blocked"}, {"available", false}},
                                      QVariantMap{{"label", "Removable storage"}, {"id", "safe"}, {"available", true}}},
                         {}, "Choose storage", false);
        model.setSelectedDevice(Kind::usb, "blocked");
        require(model.selectedDevice(Kind::usb).isEmpty());
        model.setSelectedDevice(Kind::usb, "safe");
        app.processEvents();
        require(connect->property("enabled").toBool());
        model.setSessionState(Kind::camera, false, "Stopped");
        const QString blockedId(64, 'a'), releasedId(64, 'b');
        controller.insert("usbRecoveries",
                          QVariantList{QVariantMap{{"label", "Bound"}, {"id", blockedId}, {"available", false}},
                                       QVariantMap{{"label", "Released"}, {"id", releasedId}, {"available", true}}});
        app.processEvents();
        auto* recovery = named(picker, "usbRecoveryPicker");
        auto* restore = named(picker, "usbRecoveryRestoreButton");
        recovery->setProperty("currentIndex", 0);
        app.processEvents();
        require(!restore->property("enabled").toBool());
        recovery->setProperty("currentIndex", 1);
        app.processEvents();
        require(restore->property("enabled").toBool());
        require(QMetaObject::invokeMethod(restore, "clicked"));
        require(controller.recoveryRequests == 1 && controller.recoveredId == releasedId);
        controller.insert("recoveryBusy", true);
        app.processEvents();
        require(!restore->property("enabled").toBool());
        controller.insert("recoveryBusy", false);
        controller.insert("hosting", true);
        app.processEvents();
        require(!restore->property("enabled").toBool());
        controller.insert("hosting", false);
        // Hide only redundant idle Disconnected; preserve failures and stop progress.
        model.select(3, false);
        model.setSessionState(Kind::usb, false, "Disconnected");
        app.processEvents();
        auto* usbStatus = named(picker, "resourceStatus_usb");
        require(!usbStatus->property("visible").toBool());
        model.setSessionState(Kind::usb, false, "USB recovery required on the source PC.");
        app.processEvents();
        require(usbStatus->property("visible").toBool());
        model.setSessionState(Kind::usb, true, "Returning USB to this PC...");
        app.processEvents();
        require(usbStatus->property("visible").toBool());
        model.setSessionState(Kind::usb, false, "Disconnected");
        model.select(3, true);
        app.processEvents();
        require(usbStatus->property("visible").toBool());
        model.setDevices(Kind::camera, {}, "cam-a", "Camera unplugged", false);
        app.processEvents();
        require(connect->property("enabled").toBool() && model.selectedDevice(Kind::microphone) == "mic-a");
        model.setCapability(Kind::camera, {false, "Camera unavailable"});
        app.processEvents();
        require(!model.selected(Kind::camera) && connect->property("enabled").toBool());
        auto* window = qobject_cast<QWindow*>(root);
        require(window);
        window->resize(390, 844);
        app.processEvents();
        window->resize(1120, 760);
        app.processEvents();
        require(controller.setupRequests == 0 && controller.recoveryRefreshRequests == 0 &&
                controller.ejectRequests == 0 && !warnings);
        ConnectionModel instances;
        const auto instanceDisplay = instances.addConnection("display"),
                   instanceCamera = instances.addConnection("camera"), secondCamera = instances.addConnection("camera"),
                   instanceMic = instances.addConnection("microphone");
        TestConnectionRuntime runtime(instances);
        runtime.insert("connections", QVariant::fromValue(&instances));
        runtime.insert("active", false);
        runtime.insert("status", QString("Ready"));
        runtime.insert("devices", QVariantMap{});
        runtime.insert("monitors", QVariantList{});
        runtime.insert("mirrorStates", QVariantMap{});
        ResourceModel repeatedModel;
        TestController repeated(repeatedModel);
        defaults(repeated, true);
        repeated.insert("connectionRuntime", QVariant::fromValue(&runtime));
        QQmlApplicationEngine repeatedEngine;
        watch(repeatedEngine);
        repeatedEngine.rootContext()->setContextProperty("receiver", &repeated);
        repeatedEngine.load(QUrl("qrc:/Main.qml"));
        require(!repeatedEngine.rootObjects().isEmpty());
        repeatedEngine.rootObjects().first()->setProperty("page", 1);
        app.processEvents();
        require(!warnings);
        auto* repeatedRoot = repeatedEngine.rootObjects().first();
        auto* monitorControl = named(repeatedRoot, ("displayMonitor_" + instanceDisplay).toUtf8().constData());
        require(!monitorControl->property("visible").toBool());
        for (auto* object : repeatedRoot->findChildren<QObject*>())
            require(object->property("text").toString() != "Connect to server");
        auto* disconnectAllRow = named(repeatedRoot, "connectionDisconnectAllRow");
        require(!disconnectAllRow->property("visible").toBool());
        // A retiring helper/control process is not itself a resource connection.
        runtime.insert("active", true);
        app.processEvents();
        require(!disconnectAllRow->property("visible").toBool());
        runtime.insert("active", false);
        auto monitorMode = instances.connection(instanceDisplay)["displayMode"].toMap();
        monitorMode["mode"] = "native";
        require(instances.setDisplayMode(instanceDisplay, monitorMode));
        app.processEvents();
        require(monitorControl->property("visible").toBool());
        monitorMode["mode"] = "custom";
        require(instances.setDisplayMode(instanceDisplay, monitorMode));
        app.processEvents();
        require(!monitorControl->property("visible").toBool() && !warnings);
        // Invalid attempts must mark all relevant fields without starting a runtime.
        auto* enableDisplay = named(repeatedRoot, ("connectionEnable_" + instanceDisplay).toUtf8().constData());
        auto* connectionPanel = named(repeatedRoot, "clientConnectionsPanel");
        monitorMode["mode"] = "native";
        require(instances.setDisplayMode(instanceDisplay, monitorMode));
        app.processEvents();
        require(QMetaObject::invokeMethod(enableDisplay, "clicked"));
        app.processEvents();
        require(runtime.requests.empty() && !instances.connection(instanceDisplay)["connectionEnabled"].toBool());
        require(connectionPanel->property("invalidControls").value<QJSValue>().toVariant().toList().size() >= 2);
        require(named(repeatedRoot, "connectionValidationToast")->property("visible").toBool());
        if (const auto directory = qEnvironmentVariable("EXTRAPANE_UI_SCREENSHOTS"); !directory.isEmpty()) {
            require(QDir().mkpath(directory));
            for (int i = 0; i < 35; ++i) {
                app.processEvents();
                QThread::msleep(10);
            }
            require(qobject_cast<QQuickWindow*>(repeatedRoot)->grabWindow().save(directory + "/client-validation.png"));
        }

        monitorMode["mode"] = "custom";
        require(instances.setDisplayMode(instanceDisplay, monitorMode));
        app.processEvents();
        require(instances.setSharedTarget(QVariantMap{
            {"address", "192.0.2.1"}, {"port", 43824}, {"localAddress", "0.0.0.0"}, {"interfaceIndex", 0}}));
        app.processEvents();
        auto* codeField = named(named(repeatedRoot, "sharedDestinationFields"), "connectionCode");
        codeField->setProperty("text", "12");
        require(QMetaObject::invokeMethod(enableDisplay, "clicked"));
        app.processEvents();
        require(runtime.requests.empty() &&
                connectionPanel->property("invalidControls").value<QJSValue>().toVariant().toList().size() == 1);
        codeField->setProperty("text", "");
        require(QMetaObject::invokeMethod(enableDisplay, "clicked"));
        app.processEvents();
        require(runtime.requests.size() == 1 && runtime.requests.back() == instanceDisplay &&
                instances.connection(instanceDisplay)["connectionEnabled"].toBool());
        waitFor([&] { return !named(repeatedRoot, "connectionValidationToast")->property("visible").toBool(); });
        runtime.requests.clear();
        instances.setEnabled(instanceDisplay, false);
        for (const auto& id : {instanceDisplay, instanceCamera, secondCamera, instanceMic}) {
            instances.setSource(id, "fake-source");
            instances.setEnabled(id, true);
            require(instances.beginConnection(id));
            require(instances.setState(id, ConnectionModel::State::active, "Connected"));
        }
        app.processEvents();
        require(named(repeatedRoot, "connectionSuccessToast")->property("visible").toBool());
        require(named(repeatedRoot, "connectionSuccessMessage")
                    ->property("text")
                    .toString()
                    .contains("Display connected to 192.0.2.1:43824"));
        require(named(repeatedRoot, "connectionSuccessMessage")
                    ->property("text")
                    .toString()
                    .contains("Microphone connected to 192.0.2.1:43824"));
        app.processEvents();
        auto* displayMode = named(repeatedRoot, ("displayModeChoice_" + instanceDisplay).toUtf8().constData());
        require(!displayMode->property("visible").toBool() &&
                !named(repeatedRoot, "sharedDestinationFields")->property("visible").toBool() &&
                named(repeatedRoot, "sharedDestinationSummary")->property("visible").toBool());
        auto* displaySummary = named(repeatedRoot, ("connectedDescription_" + instanceDisplay).toUtf8().constData());
        require(displaySummary->property("visible").toBool() &&
                displaySummary->property("text").toString().contains("1920") &&
                displaySummary->property("text").toString().contains("60 target FPS"));
        const auto sharedTarget = instances.sharedTarget();
        auto nextTarget = sharedTarget;
        nextTarget["address"] = "192.0.2.55";
        require(instances.setSharedTarget(nextTarget));
        app.processEvents();
        require(named(repeatedRoot, ("connectedTarget_" + instanceDisplay).toUtf8().constData())->property("text") ==
                "192.0.2.1:43824");
        require(named(repeatedRoot, "sharedDestinationSummary")->property("text") == "Connected to 192.0.2.1:43824");
        require(instances.setSharedTarget(sharedTarget));

        auto* collapse = named(repeatedRoot, "connectionGroupToggle_display");
        require(QMetaObject::invokeMethod(collapse, "clicked"));
        app.processEvents();
        require(!displayMode->property("visible").toBool() &&
                instances.connection(instanceDisplay)["phase"] == "active" && runtime.requests.isEmpty());
        require(QMetaObject::invokeMethod(collapse, "clicked"));
        app.processEvents();
        require(displayMode == named(repeatedRoot, ("displayModeChoice_" + instanceDisplay).toUtf8().constData()) &&
                !displayMode->property("visible").toBool());
        require(disconnectAllRow->property("visible").toBool());
        auto* camerasDisconnect = named(repeatedRoot, "connectionGroupDisconnect_camera");
        require(camerasDisconnect->property("enabled").toBool());
        require(QMetaObject::invokeMethod(camerasDisconnect, "clicked"));
        app.processEvents();
        require(runtime.requests == QStringList{instanceCamera, secondCamera} &&
                instances.connection(instanceDisplay)["phase"] == "active" &&
                instances.connection(instanceMic)["phase"] == "active" &&
                !instances.connection(instanceCamera)["connectionEnabled"].toBool() &&
                !instances.connection(secondCamera)["connectionEnabled"].toBool());
        auto* micDisconnect = named(repeatedRoot, ("connectionDisconnect_" + instanceMic).toUtf8().constData());
        require(micDisconnect->property("destructive").toBool());
        require(QMetaObject::invokeMethod(micDisconnect, "clicked"));
        app.processEvents();
        require(runtime.requests.back() == instanceMic && instances.connection(instanceMic)["phase"] == "idle" &&
                instances.connection(instanceDisplay)["phase"] == "active");
        instances.setEnabled(instanceMic, true);
        require(instances.beginConnection(instanceMic));
        require(instances.setState(instanceMic, ConnectionModel::State::active, "Connected"));
        const auto beforeAdd = instances.rowCount();
        require(QMetaObject::invokeMethod(named(repeatedRoot, "connectionGroupAdd_camera"), "clicked"));
        app.processEvents();
        require(instances.rowCount() == beforeAdd + 1 &&
                instances.data(instances.index(beforeAdd), ConnectionModel::Kind) == "camera" && !warnings);
        auto* nav = named(repeatedRoot, "navigationSidebar");
        auto* navToggle = named(repeatedRoot, "sidebarToggle");
        auto* repeatedWindow = qobject_cast<QQuickWindow*>(repeatedRoot);
        require(repeatedWindow);
        repeatedWindow->resize(1120, 760);
        app.processEvents();
        const auto expandedWidth = nav->property("width").toReal();
        require(QMetaObject::invokeMethod(navToggle, "clicked"));
        waitFor([&] { return nav->property("width").toReal() < expandedWidth; });
        require(repeatedRoot->property("sidebarCollapsed").toBool() &&
                instances.connection(instanceDisplay)["phase"] == "active");
        require(QMetaObject::invokeMethod(named(repeatedRoot, "navigation_about"), "clicked"));
        app.processEvents();
        require(repeatedRoot->property("page") == 3);
        require(QMetaObject::invokeMethod(navToggle, "clicked"));
        app.processEvents();
        require(!repeatedRoot->property("sidebarCollapsed").toBool());
        for (const auto* key : {"display", "camera", "microphone", "usb"}) {
            auto* title = named(repeatedRoot, (QString("connectionGroupTitle_") + key).toUtf8().constData());
            auto* item = qobject_cast<QQuickItem*>(title);
            require(item && qAbs(item->x() + item->width() / 2 - item->parentItem()->width() / 2) < 1.0);
        }
        const auto screenshotPath = qEnvironmentVariable("EXTRAPANE_UI_SCREENSHOTS");
        if (!screenshotPath.isEmpty()) {
            require(QDir().mkpath(screenshotPath));
            auto capture = [&](QString name) {
                for (int i = 0; i < 35; ++i) {
                    app.processEvents();
                    QThread::msleep(10);
                }
                const auto image = repeatedWindow->grabWindow();
                require(!image.isNull() && image.save(screenshotPath + "/" + name + ".png"));
            };
            repeated.insert("hostComponents", QVariantList{});
            repeated.insert("hostDescription",
                            QString("Clients choose which displays, cameras, microphones and USB devices to share."));
            runtime.insert("devices", QVariantMap{{"camera", devices("fake-source", "Surface camera")},
                                                  {"microphone", devices("fake-source", "Surface microphone")}});
            runtime.insert("active", true);
            repeated.insert("hostStatus", QString("Ready to accept connections"));
            repeatedRoot->setProperty("page", 0);
            capture("server-1120");
            repeated.insert("hosting", true);
            capture("server-stop-1120");
            repeated.insert("hosting", false);
            repeatedRoot->setProperty("page", 1);
            capture("client-1120");
            repeatedWindow->resize(720, 760);
            repeatedRoot->setProperty("sidebarCollapsed", false);
            capture("client-720-expanded");
            repeatedRoot->setProperty("sidebarCollapsed", true);
            capture("client-720-collapsed");
            while (instances.rowCount()) {
                const auto id = instances.data(instances.index(0), ConnectionModel::Id).toString();
                instances.setState(id, ConnectionModel::State::idle);
                require(instances.removeConnection(id));
            }
            runtime.insert("active", false);
            repeatedWindow->resize(1120, 760);
            repeatedRoot->setProperty("sidebarCollapsed", false);
            capture("client-empty-1120");
            auto* destination = named(repeatedRoot, "sharedDestinationFields");
            auto* address = named(destination, "connectionAddress");
            address->setProperty("text", "invalid-host");
            require(QMetaObject::invokeMethod(destination, "save"));
            require(QMetaObject::invokeMethod(destination, "validate"));
            capture("client-invalid-address");
            repeatedRoot->setProperty("sidebarCollapsed", true);
            capture("client-invalid-collapsed");
            address->setProperty("text", "192.0.2.1");
            require(QMetaObject::invokeMethod(destination, "save"));
        }
        for (int index = 0; index < instances.rowCount(); ++index) {
            const auto id = instances.data(instances.index(index), ConnectionModel::Id).toString();
            instances.setState(id, ConnectionModel::State::idle);
        }
        repeatedRoot->setProperty("page", 1);
        app.processEvents();
        require(named(repeatedRoot, "sharedDestinationFields")->property("visible").toBool() &&
                !named(repeatedRoot, "sharedDestinationSummary")->property("visible").toBool());
        require(!warnings);
        std::cout << "Shared UI multi-resource model and offscreen shell checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#include "shared_ui_check.moc"
