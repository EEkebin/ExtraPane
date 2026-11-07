#include "resource_model.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QWindow>
#include <QQuickStyle>
#include <QVariantList>
#include <QVariantMap>
#include <iostream>
#include <stdexcept>
#include <source_location>

static void require(bool condition,const std::source_location where=std::source_location::current()){
    if(!condition)throw std::runtime_error("Shared UI model check failed at line "+std::to_string(where.line()));
}
class TestController final : public QQmlPropertyMap {
    Q_OBJECT
public:
    TestController():QQmlPropertyMap(this,nullptr){}
    Q_INVOKABLE void releaseInput(){}
    Q_INVOKABLE void refreshCameras(){++refreshRequests;}
    Q_INVOKABLE void disconnectFromServer(){++disconnectRequests;}
    Q_INVOKABLE void setupCamera(){++setupRequests;}
    Q_INVOKABLE void setupMicrophone(){++setupRequests;}
    Q_INVOKABLE void setupUsbSource(){++setupRequests;}
    Q_INVOKABLE void refreshUsbRecovery(){++recoveryRefreshRequests;}
    Q_INVOKABLE void recoverUsb(QString id){++recoveryRequests;recoveredId=std::move(id);}
    Q_INVOKABLE void startHost(QString address,int port,bool code){++hostStartRequests;startedAddress=std::move(address);startedPort=port;startedRequireCode=code;}
    Q_INVOKABLE void stopHost(){++hostStopRequests;}
    Q_INVOKABLE void ejectUsb(){++ejectRequests;}
    int refreshRequests=0;
    int disconnectRequests=0;
    int setupRequests=0,recoveryRefreshRequests=0,recoveryRequests=0,hostStartRequests=0,hostStopRequests=0,ejectRequests=0;
    QString startedAddress;int startedPort=0;bool startedRequireCode=false;
    QString recoveredId;
};
static QObject* named(QObject* root,const char* name){
    auto* object=root->findChild<QObject*>(QString::fromLatin1(name));
    if(!object)throw std::runtime_error(std::string("Missing UI control: ")+name);
    return object;
}
int main(int argc,char** argv){
    qputenv("QT_QPA_PLATFORM","offscreen");
    qputenv("QT_QUICK_BACKEND","software");
    QQuickStyle::setStyle("Fusion");
    QGuiApplication app(argc,argv);
    try{
        ResourceModel model;
        require(!model.canConnect());
        model.setCapability(extrapane::ResourceKind::display,{true,{}});
        require(model.canConnect());
        model.select(0,false);require(!model.canConnect());
        model.select(1,true);require(!model.canConnect());
        model.select(0,true);model.setLocked(true);
        model.select(0,false);require(model.selected(extrapane::ResourceKind::display));
        require(!model.canConnect());model.setLocked(false);require(model.canConnect());
        model.setCapability(extrapane::ResourceKind::display,{false,"Device removed"});
        require(!model.canConnect());
        TestController controller;
        controller.insert("resources",QVariant::fromValue(&model));
        for(const auto* key:{"accessCode","hostStatus","savedAddress","statistics","decoder","version","platformLabel","hostDescription","inputDescription","rendererDescription"})controller.insert(key,QString{});
        controller.insert("hostAvailability",QString("Headless check"));
        controller.insert("platformLabel",QString("Linux"));
        controller.insert("availability",QString("Headless check"));
        controller.insert("active",false);controller.insert("hosting",false);
        controller.insert("status",QString("Ready"));
        controller.insert("sourceWidth",1280);controller.insert("sourceHeight",720);
        controller.insert("addresses",QVariantList{});
        QQmlApplicationEngine engine;
        bool warnings=false;
        QObject::connect(&engine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError>& errors){
            warnings=true;for(const auto& error:errors)std::cerr<<error.toString().toStdString()<<'\n';
        });
        engine.rootContext()->setContextProperty("receiver",&controller);
        engine.load(QUrl("qrc:/Main.qml"));
        require(!engine.rootObjects().isEmpty());
        app.processEvents();
        auto* window=qobject_cast<QWindow*>(engine.rootObjects().first());
        require(window!=nullptr);
        window->resize(390,844);app.processEvents();
        window->resize(1120,760);app.processEvents();
        require(!warnings);
        // A second engine exercises Windows-only bindings. The first deliberately
        // omits those properties so the Linux fallback cannot accidentally depend
        // on a Windows controller or camera inventory.
        TestController camera;
        ResourceModel cameraModel;
        cameraModel.select(0,false);
        camera.insert("resources",QVariant::fromValue(&cameraModel));
        for(const auto& key:controller.keys())if(key!="resources")camera.insert(key,controller.value(key));
        camera.insert("platformLabel",QString("Windows"));
        camera.insert("hostAvailability",QString{});
        camera.insert("hostComponents",QVariantList{
            QVariantMap{{"label","Display"},{"available",true},{"detail","Driver checked on connection"}},
            QVariantMap{{"label","Camera"},{"available",false},{"detail","Camera setup required"}},
            QVariantMap{{"label","Microphone"},{"available",false},{"detail","Receiving component setup required"}},
            QVariantMap{{"label","USB storage"},{"available",false},{"detail","Receiving component setup required"}}
        });
        camera.insert("clientMode",QString("camera"));
        camera.insert("availability",QString{});
        camera.insert("cameraSetupRunning",false);
        camera.insert("cameras",QVariantList{});
        camera.insert("cameraScanRunning",false);
        camera.insert("cameraStatus",QString("No cameras found"));
        camera.insert("selectedCamera",QString{});
        camera.insert("usbRecoveries",QVariantList{});
        camera.insert("recoveryStatus",QString{});
        camera.insert("recoveryBusy",false);
        QQmlApplicationEngine cameraEngine;
        QObject::connect(&cameraEngine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError>& errors){
            warnings=true;for(const auto& error:errors)std::cerr<<error.toString().toStdString()<<'\n';
        });
        cameraEngine.rootContext()->setContextProperty("receiver",&camera);
        cameraEngine.load(QUrl("qrc:/Main.qml"));
        require(!cameraEngine.rootObjects().isEmpty());
        app.processEvents();
        require(camera.refreshRequests==0&&camera.setupRequests==0&&camera.hostStartRequests==0&&camera.recoveryRefreshRequests==0);
        auto* cameraRoot=cameraEngine.rootObjects().first();
        // One server starts independently of component readiness. Missing camera
        // or microphone setup is information, never an exclusive server mode.
        cameraRoot->setProperty("page",0);app.processEvents();
        auto* serverPage=named(cameraRoot,"serverPage");
        auto* serverStart=named(cameraRoot,"serverStartButton");
        auto* hostAddress=named(cameraRoot,"hostAddressPicker");
        auto* components=named(cameraRoot,"hostComponentsRepeater");
        require(serverPage->property("visible").toBool());
        require(components->property("count").toInt()==4);
        int serverComboCount=0;
        for(auto* object:serverPage->findChildren<QObject*>())if(object->inherits("QQuickComboBox")){
            ++serverComboCount;require(object==hostAddress);
        }
        require(serverComboCount==1); // The address picker is the only server dropdown.
        require(serverStart->property("enabled").toBool());
        require(serverStart->property("text").toString()=="Start server");
        require(QMetaObject::invokeMethod(serverStart,"clicked"));
        require(camera.hostStartRequests==1&&camera.startedAddress=="0.0.0.0"&&camera.startedPort==43824&&!camera.startedRequireCode);
        require(camera.setupRequests==0&&camera.refreshRequests==0);
        camera.insert("hosting",true);app.processEvents();
        require(serverStart->property("text").toString()=="Stop server");
        camera.insert("hostAvailability",QString("Server helper is missing"));app.processEvents();
        require(serverStart->property("enabled").toBool()); // An active server can always be stopped.
        require(QMetaObject::invokeMethod(serverStart,"clicked"));require(camera.hostStopRequests==1);
        camera.insert("hosting",false);app.processEvents();require(!serverStart->property("enabled").toBool());
        camera.insert("hostAvailability",QString{});camera.insert("cameraSetupRunning",true);app.processEvents();
        require(!serverStart->property("enabled").toBool());
        camera.insert("cameraSetupRunning",false);app.processEvents();require(serverStart->property("enabled").toBool());
        cameraRoot->setProperty("page",1);app.processEvents();
        auto* panel=named(cameraRoot,"cameraClientPanel");
        auto* picker=named(cameraRoot,"cameraSourcePicker");
        auto* refresh=named(cameraRoot,"cameraRefreshButton");
        auto* connect=named(cameraRoot,"connectButton");
        auto* video=named(cameraRoot,"clientVideoLoader");
        auto* disconnect=named(cameraRoot,"cameraDisconnectButton");
        require(panel->property("visible").toBool());
        require(!video->property("active").toBool());
        require(!disconnect->property("visible").toBool());
        require(picker->property("count").toInt()==0);
        require(!connect->property("enabled").toBool());
        require(QMetaObject::invokeMethod(refresh,"clicked"));
        require(camera.refreshRequests==1);
        camera.insert("cameraScanRunning",true);app.processEvents();
        require(!refresh->property("enabled").toBool());
        require(!picker->property("enabled").toBool());
        const QVariantList inventory{
            QVariantMap{{"label","Built-in camera"},{"id","63616d31"}},
            QVariantMap{{"label","USB camera"},{"id","63616d32"}}
        };
        camera.insert("cameras",inventory);
        camera.insert("selectedCamera",QString("63616d32"));
        camera.insert("cameraScanRunning",false);
        cameraModel.setCapability(extrapane::ResourceKind::camera,{true,{}});
        cameraModel.select(1,true);app.processEvents();
        require(picker->property("count").toInt()==2);
        require(picker->property("currentIndex").toInt()==1);
        require(connect->property("enabled").toBool());
        camera.insert("selectedCamera",QString("removed-id"));app.processEvents();
        require(!connect->property("enabled").toBool());
        camera.insert("selectedCamera",QString("63616d32"));app.processEvents();
        cameraModel.setLocked(true);camera.insert("active",true);app.processEvents();
        require(!connect->property("enabled").toBool());
        require(!picker->property("enabled").toBool());
        require(!refresh->property("enabled").toBool());
        require(disconnect->property("visible").toBool());
        require(QMetaObject::invokeMethod(disconnect,"clicked"));
        require(camera.disconnectRequests==1);
        require(!video->property("active").toBool());
        camera.insert("active",false);cameraModel.setLocked(false);
        camera.insert("cameras",QVariantList{});
        camera.insert("selectedCamera",QString{});
        cameraModel.setCapability(extrapane::ResourceKind::camera,{false,"Camera disconnected"});
        app.processEvents();
        require(!connect->property("enabled").toBool());
        require(picker->property("count").toInt()==0);
        camera.insert("microphoneMuted",false);camera.insert("clientMode",QString("microphone"));app.processEvents();
        require(!video->property("active").toBool());require(picker->property("displayText").toString()=="Select a microphone");
        cameraRoot->setProperty("page",0);app.processEvents();
        cameraRoot->setProperty("page",2);app.processEvents();
        cameraRoot->setProperty("page",3);app.processEvents();
        require(!warnings);
        // Load directly into USB mode to catch accidental startup enumeration,
        // setup, recovery, or host creation. Every action here is a fake counter.
        TestController usb;
        for(const auto& key:camera.keys())usb.insert(key,camera.value(key));
        usb.insert("clientMode",QString("usb"));
        usb.insert("cameraStatus",QString("Choose local storage"));
        const QVariantList storage{
            QVariantMap{{"label","System disk (blocked)"},{"id","blocked"},{"available",false}},
            QVariantMap{{"label","Removable storage"},{"id","safe"},{"available",true}}
        };
        usb.insert("cameras",storage);usb.insert("selectedCamera",QString("blocked"));
        const QString blockedJournal(64,'a'),releasedJournal(64,'b');
        usb.insert("usbRecoveries",QVariantList{
            QVariantMap{{"label","Disk 7 — bound; eject not verified"},{"id",blockedJournal},{"available",false}},
            QVariantMap{{"label","Disk 8 — released"},{"id",releasedJournal},{"available",true}}
        });
        QQmlApplicationEngine usbEngine;
        QObject::connect(&usbEngine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError>& errors){
            warnings=true;for(const auto& error:errors)std::cerr<<error.toString().toStdString()<<'\n';
        });
        usbEngine.rootContext()->setContextProperty("receiver",&usb);
        usbEngine.load(QUrl("qrc:/Main.qml"));require(!usbEngine.rootObjects().isEmpty());app.processEvents();
        require(usb.refreshRequests==0&&usb.setupRequests==0&&usb.hostStartRequests==0&&usb.recoveryRefreshRequests==0&&usb.recoveryRequests==0&&usb.ejectRequests==0);
        auto* usbRoot=usbEngine.rootObjects().first();
        auto* usbConnect=named(usbRoot,"connectButton");
        auto* usbPicker=named(usbRoot,"cameraSourcePicker");
        auto* usbRefresh=named(usbRoot,"cameraRefreshButton");
        auto* setup=named(usbRoot,"usbSourceSetupButton");
        auto* recoveryPicker=named(usbRoot,"usbRecoveryPicker");
        auto* recoveryRefresh=named(usbRoot,"usbRecoveryRefreshButton");
        auto* restore=named(usbRoot,"usbRecoveryRestoreButton");
        require(!named(usbRoot,"clientVideoLoader")->property("active").toBool());
        require(usbPicker->property("count").toInt()==2&&!usbConnect->property("enabled").toBool());
        require(recoveryPicker->property("count").toInt()==2);
        recoveryPicker->setProperty("currentIndex",0);app.processEvents();
        require(!restore->property("enabled").toBool());
        usb.insert("selectedCamera",QString("safe"));app.processEvents();
        require(usbConnect->property("enabled").toBool());
        usb.insert("cameraScanRunning",true);app.processEvents();
        require(!usbConnect->property("enabled").toBool()&&!usbPicker->property("enabled").toBool());
        usb.insert("cameraScanRunning",false);usb.insert("availability",QString("USB source component is missing"));app.processEvents();
        require(!usbConnect->property("enabled").toBool());
        usb.insert("availability",QString{});usb.insert("selectedCamera",QString("removed-device"));app.processEvents();
        require(!usbConnect->property("enabled").toBool());
        usb.insert("selectedCamera",QString("safe"));
        recoveryPicker->setProperty("currentIndex",1);app.processEvents();require(restore->property("enabled").toBool());
        require(QMetaObject::invokeMethod(restore,"clicked"));require(usb.recoveryRequests==1&&usb.recoveredId==releasedJournal);
        usb.insert("recoveryBusy",true);app.processEvents();
        require(!restore->property("enabled").toBool()&&!recoveryRefresh->property("enabled").toBool()&&!recoveryPicker->property("enabled").toBool());
        usb.insert("recoveryBusy",false);usb.insert("active",true);app.processEvents();
        require(!usbConnect->property("enabled").toBool()&&!usbRefresh->property("enabled").toBool()&&!setup->property("enabled").toBool()&&!restore->property("enabled").toBool());
        usb.insert("active",false);usb.insert("hosting",true);app.processEvents();
        require(!restore->property("enabled").toBool()&&!recoveryRefresh->property("enabled").toBool()&&!setup->property("enabled").toBool());
        usb.insert("hosting",false);app.processEvents();
        require(QMetaObject::invokeMethod(recoveryRefresh,"clicked"));require(usb.recoveryRefreshRequests==1);
        require(usb.refreshRequests==0&&usb.setupRequests==0&&usb.hostStartRequests==0&&usb.ejectRequests==0);
        require(!warnings);
        std::cout<<"Shared UI model and offscreen shell checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#include "shared_ui_check.moc"
