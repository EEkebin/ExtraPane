#include "resource_model.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QWindow>
#include <QQuickItem>
#include <QSet>
#include <QQuickStyle>
#include <QVariantList>
#include <QVariantMap>
#include <iostream>
#include <stdexcept>
#include <source_location>

using Kind=extrapane::ResourceKind;
static void require(bool condition,const std::source_location where=std::source_location::current()){
    if(!condition)throw std::runtime_error("Shared UI model check failed at line "+std::to_string(where.line()));
}
static Kind kind(const QString& key){if(key=="display")return Kind::display;if(key=="camera")return Kind::camera;if(key=="microphone")return Kind::microphone;if(key=="usb")return Kind::usb;throw std::runtime_error("Unexpected resource action");}
class TestController final : public QQmlPropertyMap {
    Q_OBJECT
public:
    explicit TestController(ResourceModel& resources):QQmlPropertyMap(this,nullptr),model(resources){
        insert("resources",QVariant::fromValue(&model));
        connect(&model,&ResourceModel::selectionChanged,this,[this]{insert("clientReady",true);});
        insert("clientReady",true);
    }
    Q_INVOKABLE void releaseInput(){}
    Q_INVOKABLE void refreshDevices(QString key){++refreshRequests;refreshed=std::move(key);}
    Q_INVOKABLE void selectDevice(QString key,QString id){++deviceSelections;model.setSelectedDevice(kind(key),std::move(id));}
    Q_INVOKABLE void stopResource(QString key){++stopRequests;stopped=key;model.setSessionState(kind(key),false,"Stopped");}
    Q_INVOKABLE void setCameraMirrored(bool value){++mirrorRequests;insert("cameraMirrored",value);}
    Q_INVOKABLE void setMicrophoneMuted(bool value){++muteRequests;model.setSessionState(Kind::microphone,true,"Sharing",value);}
    Q_INVOKABLE void disconnectFromServer(){++disconnectRequests;for(int i=0;i<model.rowCount();++i)model.select(i,false);}
    Q_INVOKABLE void connectTo(QString,QString,bool,unsigned,unsigned,unsigned,QString){++connectRequests;}
    Q_INVOKABLE void setupUsbSource(){++setupRequests;}
    Q_INVOKABLE void refreshUsbRecovery(){++recoveryRefreshRequests;}
    Q_INVOKABLE void recoverUsb(QString id){++recoveryRequests;recoveredId=std::move(id);}
    Q_INVOKABLE void startHost(QString address,int port,bool code){++hostStartRequests;startedAddress=std::move(address);startedPort=port;startedRequireCode=code;}
    Q_INVOKABLE void stopHost(){++hostStopRequests;}
    Q_INVOKABLE void ejectUsb(){++ejectRequests;}
    ResourceModel& model;
    int refreshRequests=0,disconnectRequests=0,deviceSelections=0,stopRequests=0,muteRequests=0,connectRequests=0,mirrorRequests=0;
    int setupRequests=0,recoveryRefreshRequests=0,recoveryRequests=0,hostStartRequests=0,hostStopRequests=0,ejectRequests=0;
    QString startedAddress,refreshed,stopped,recoveredId;int startedPort=0;bool startedRequireCode=false;
};
static QObject* named(QObject* root,const char* name){
    const auto wanted=QString::fromLatin1(name);QSet<QObject*> seen;
    std::vector<QObject*> pending{root};
    while(!pending.empty()){
        auto* object=pending.back();pending.pop_back();if(seen.contains(object))continue;seen.insert(object);
        if(object->objectName()==wanted)return object;
        for(auto* child:object->children())pending.push_back(child);
        if(auto* item=qobject_cast<QQuickItem*>(object))for(auto* child:item->childItems())pending.push_back(child);
    }
    throw std::runtime_error(std::string("Missing UI control: ")+name);
}
static void defaults(TestController& controller,bool windows){
    for(const auto* key:{"accessCode","hostStatus","savedAddress","statistics","decoder","version","hostDescription","inputDescription","rendererDescription","recoveryStatus"})controller.insert(key,QString{});
    controller.insert("platformLabel",QString(windows?"Windows":"Linux"));
    controller.insert("hostAvailability",QString(windows?"":"Headless check"));
    controller.insert("availability",QString(windows?"":"Headless check"));
    controller.insert("active",false);controller.insert("hosting",false);controller.insert("cameraMirrored",false);
    controller.insert("status",QString("Ready"));controller.insert("sourceWidth",1280);controller.insert("sourceHeight",720);
    controller.insert("addresses",QVariantList{QVariantMap{{"label","Automatic route"},{"address","0.0.0.0"},{"index",0}}});
    if(windows){
        controller.insert("cameraSetupRunning",false);controller.insert("recoveryBusy",false);controller.insert("usbRecoveries",QVariantList{});
        controller.insert("hostComponents",QVariantList{
            QVariantMap{{"label","Display"},{"available",true},{"detail","Driver checked on connection"}},
            QVariantMap{{"label","Camera"},{"available",false},{"detail","Camera setup required"}},
            QVariantMap{{"label","Microphone"},{"available",false},{"detail","Receiving component setup required"}},
            QVariantMap{{"label","USB storage"},{"available",false},{"detail","Receiving component setup required"}}
        });
    }
}
static QVariantList devices(const QString& id,const QString& label){return {QVariantMap{{"label",label},{"id",id},{"available",true}}};}
static void model_checks(){
    ResourceModel model;require(!model.canConnect());
    model.setCapability(Kind::display,{true,{}});require(model.canConnect());
    model.setCapability(Kind::camera,{true,{}});model.setCapability(Kind::microphone,{true,{}});model.setCapability(Kind::usb,{true,{}});
    model.select(1,true);model.select(2,true);require(model.selected(Kind::display)&&model.selected(Kind::camera)&&model.selected(Kind::microphone));require(!model.canConnect());
    model.setDevices(Kind::camera,devices("camera-a","Camera"),{},"Choose camera",false);require(model.selectedDevice(Kind::camera).isEmpty());
    model.setSelectedDevice(Kind::camera,"camera-a");require(!model.canConnect());
    model.setDevices(Kind::microphone,devices("mic-a","Microphone"),{},"Choose mic",false);model.setSelectedDevice(Kind::microphone,"mic-a");require(model.canConnect());
    model.setDevices(Kind::usb,devices("usb-a","Storage"),{},"Scanning unused resource",true);require(model.canConnect());
    model.setDevices(Kind::camera,model.devices(Kind::camera),"camera-a","Scanning selected resource",true);require(!model.canConnect());
    model.setSelectedDevice(Kind::camera,{});require(model.selectedDevice(Kind::camera)=="camera-a");
    model.setDevices(Kind::camera,model.devices(Kind::camera),"camera-a","Ready",false);require(model.canConnect());
    model.setSelectedDevice(Kind::camera,"missing");require(model.selectedDevice(Kind::camera)=="camera-a");
    model.setLocked(true);model.select(1,false);model.setSelectedDevice(Kind::microphone,{});require(model.selected(Kind::camera)&&model.selectedDevice(Kind::microphone)=="mic-a"&&!model.canConnect());
    model.setSessionState(Kind::camera,true,"Sharing camera");model.setSessionState(Kind::microphone,true,"Sharing microphone",true);
    model.setSessionState(Kind::camera,false,"Camera stopped");require(model.data(model.index(2),ResourceModel::Running).toBool()&&model.data(model.index(2),ResourceModel::Muted).toBool());
    model.setLocked(false);model.setDevices(Kind::camera,devices("camera-b","Replacement"),"camera-a","Camera changed",false);
    require(model.selectedDevice(Kind::camera).isEmpty()&&model.selectedDevice(Kind::microphone)=="mic-a"&&!model.canConnect());
    model.setCapability(Kind::camera,{false,"Unavailable"});require(!model.selected(Kind::camera)&&model.selected(Kind::microphone));require(model.canConnect());
    model.setCapability(Kind::display,{false,"Unavailable"});require(!model.selected(Kind::display)&&model.canConnect());
    model.setDevices(Kind::microphone,QVariantList{QVariantMap{{"label","Blocked"},{"id","blocked"},{"available",false}}},{},"Blocked",false);model.setSelectedDevice(Kind::microphone,"blocked");require(model.selectedDevice(Kind::microphone).isEmpty()&&!model.canConnect());
    auto duplicate=devices("duplicate","Mic one");duplicate+=devices("duplicate","Mic two");model.setDevices(Kind::microphone,duplicate,"duplicate","Ambiguous",false);require(model.selectedDevice(Kind::microphone).isEmpty());
}
int main(int argc,char** argv){
    qputenv("QT_QPA_PLATFORM","offscreen");qputenv("QT_QUICK_BACKEND","software");QQuickStyle::setStyle("Fusion");QGuiApplication app(argc,argv);
    try{
        model_checks();bool warnings=false;
        auto watch=[&](QQmlApplicationEngine& engine){QObject::connect(&engine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError>& errors){warnings=true;for(const auto& error:errors)std::cerr<<error.toString().toStdString()<<'\n';});};
        ResourceModel linuxModel;TestController linux(linuxModel);defaults(linux,false);
        QQmlApplicationEngine linuxEngine;watch(linuxEngine);linuxEngine.rootContext()->setContextProperty("receiver",&linux);linuxEngine.load(QUrl("qrc:/Main.qml"));require(!linuxEngine.rootObjects().isEmpty());app.processEvents();
        auto* linuxWindow=qobject_cast<QWindow*>(linuxEngine.rootObjects().first());require(linuxWindow);linuxWindow->resize(390,844);app.processEvents();linuxWindow->resize(1120,760);app.processEvents();require(!warnings);
        ResourceModel model;for(auto k:{Kind::display,Kind::camera,Kind::microphone,Kind::usb})model.setCapability(k,{true,{}});
        TestController controller(model);defaults(controller,true);
        QQmlApplicationEngine engine;watch(engine);engine.rootContext()->setContextProperty("receiver",&controller);engine.load(QUrl("qrc:/Main.qml"));require(!engine.rootObjects().isEmpty());app.processEvents();
        require(controller.refreshRequests==0&&controller.setupRequests==0&&controller.hostStartRequests==0&&controller.recoveryRefreshRequests==0&&controller.connectRequests==0);
        auto* root=engine.rootObjects().first();root->setProperty("page",0);app.processEvents();
        auto* server=named(root,"serverPage");auto* start=named(root,"serverStartButton");auto* address=named(root,"hostAddressPicker");require(named(root,"hostComponentsRepeater")->property("count").toInt()==4);
        int serverCombos=0;for(auto* object:server->findChildren<QObject*>())if(object->inherits("QQuickComboBox")){++serverCombos;require(object==address);}require(serverCombos==1);
        for(auto* object:server->findChildren<QObject*>()){const auto text=object->property("text").toString();require(text!="Camera setup"&&text!="Receiving component setup");}
        require(start->property("enabled").toBool());require(QMetaObject::invokeMethod(start,"clicked"));require(controller.hostStartRequests==1&&controller.startedAddress=="0.0.0.0"&&controller.startedPort==43824&&!controller.startedRequireCode);
        controller.insert("hosting",true);controller.insert("hostAvailability",QString("Unavailable"));app.processEvents();require(start->property("enabled").toBool());require(QMetaObject::invokeMethod(start,"clicked"));require(controller.hostStopRequests==1);
        controller.insert("hosting",false);app.processEvents();require(!start->property("enabled").toBool());controller.insert("hostAvailability",QString{});controller.insert("cameraSetupRunning",true);app.processEvents();require(!start->property("enabled").toBool());require(start->property("text").toString()=="Preparing server...");require(!address->property("enabled").toBool());controller.insert("cameraSetupRunning",false);
        root->setProperty("page",1);app.processEvents();
        auto* picker=named(root,"resourcePicker");auto* connect=named(root,"connectButton");auto* disconnect=named(root,"clientDisconnectButton");
        auto* display=named(picker,"resourceCheck_display");auto* camera=named(picker,"resourceCheck_camera");auto* microphone=named(picker,"resourceCheck_microphone");auto* usb=named(picker,"resourceCheck_usb");
        require(display->property("checked").toBool()&&camera->property("enabled").toBool()&&microphone->property("enabled").toBool()&&usb->property("enabled").toBool());
        require(!named(root,"clientVideoLoader")->property("active").toBool());require(connect->property("enabled").toBool());
        // Check multiple resources through the actual checkbox signals; no fake
        // controller inventory, capture, setup or connection runs automatically.
        camera->setProperty("checked",true);require(QMetaObject::invokeMethod(camera,"clicked"));microphone->setProperty("checked",true);require(QMetaObject::invokeMethod(microphone,"clicked"));app.processEvents();
        require(model.selected(Kind::display)&&model.selected(Kind::camera)&&model.selected(Kind::microphone)&&connect->property("enabled").toBool());
        auto* mirror=named(picker,"resourceMirror_camera");require(mirror->property("visible").toBool()&&mirror->property("enabled").toBool()&&!mirror->property("checked").toBool());
        mirror->setProperty("checked",true);require(QMetaObject::invokeMethod(mirror,"clicked"));app.processEvents();require(controller.mirrorRequests==1&&controller.value("cameraMirrored").toBool()&&controller.connectRequests==0);
        auto* cameraPicker=named(picker,"resourceDevicePicker_camera");auto* micPicker=named(picker,"resourceDevicePicker_microphone");
        require(cameraPicker->property("visible").toBool()&&micPicker->property("visible").toBool());auto* cameraPopup=cameraPicker->property("popup").value<QObject*>();require(cameraPopup&&cameraPicker->property("enabled").toBool()&&cameraPicker->property("count").toInt()==0);
        require(QMetaObject::invokeMethod(cameraPopup,"open"));for(int tick=0;tick<10;++tick)app.processEvents();require(controller.refreshed=="camera"&&controller.refreshRequests==1);require(QMetaObject::invokeMethod(cameraPopup,"close"));
        for(auto* object:picker->findChildren<QObject*>()){require(!object->objectName().startsWith("resourceRefresh_"));require(object->property("text").toString()!="USB source setup");}
        model.setDevices(Kind::camera,devices("cam-a","Built-in camera"),{},"Choose camera",false);model.setDevices(Kind::microphone,devices("mic-a","Built-in microphone"),{},"Choose microphone",false);app.processEvents();
        require(cameraPicker->property("currentIndex").toInt()==-1&&micPicker->property("currentIndex").toInt()==-1);
        cameraPicker->setProperty("currentIndex",0);require(QMetaObject::invokeMethod(cameraPicker,"activated",Q_ARG(int,0)));micPicker->setProperty("currentIndex",0);require(QMetaObject::invokeMethod(micPicker,"activated",Q_ARG(int,0)));app.processEvents();
        require(controller.deviceSelections==2&&model.selectedDevice(Kind::camera)=="cam-a"&&model.selectedDevice(Kind::microphone)=="mic-a"&&connect->property("enabled").toBool());
        model.setDevices(Kind::camera,model.devices(Kind::camera),"cam-a","Listing devices...",true);app.processEvents();
        require(cameraPicker->property("enabled").toBool());require(QMetaObject::invokeMethod(cameraPopup,"open"));for(int tick=0;tick<10;++tick)app.processEvents();require(controller.refreshRequests==1);require(QMetaObject::invokeMethod(cameraPopup,"close"));
        model.setDevices(Kind::camera,model.devices(Kind::camera),"cam-a","Ready",false);app.processEvents();
        require(QMetaObject::invokeMethod(cameraPopup,"open"));for(int tick=0;tick<10;++tick)app.processEvents();require(controller.refreshRequests==2&&model.selectedDevice(Kind::camera)=="cam-a");require(QMetaObject::invokeMethod(cameraPopup,"close"));
        require(QMetaObject::invokeMethod(connect,"clicked"));require(controller.connectRequests==1);
        model.setLocked(false);controller.insert("active",true);model.setSessionState(Kind::display,true,"Receiving");model.setSessionState(Kind::camera,true,"Sharing");model.setSessionState(Kind::microphone,true,"Sharing");app.processEvents();
        require(picker->property("visible").toBool()&&camera->property("enabled").toBool()&&!cameraPicker->property("enabled").toBool()&&disconnect->property("visible").toBool());
        require(QMetaObject::invokeMethod(cameraPopup,"open"));for(int tick=0;tick<10;++tick)app.processEvents();require(controller.refreshRequests==2);require(QMetaObject::invokeMethod(cameraPopup,"close"));
        require(mirror->property("visible").toBool()&&mirror->property("enabled").toBool());
        mirror->setProperty("checked",false);require(QMetaObject::invokeMethod(mirror,"clicked"));app.processEvents();require(controller.mirrorRequests==2&&!controller.value("cameraMirrored").toBool()&&controller.connectRequests==1&&controller.disconnectRequests==0&&controller.stopRequests==0&&controller.muteRequests==0);
        camera->setProperty("checked",false);require(QMetaObject::invokeMethod(camera,"clicked"));app.processEvents();require(!model.selected(Kind::camera)&&model.selected(Kind::microphone)&&model.selected(Kind::display));
        for(auto* object:picker->findChildren<QObject*>())require(!object->objectName().startsWith("resourceStop_"));

        require(QMetaObject::invokeMethod(named(picker,"resourceMute_microphone"),"clicked"));require(controller.muteRequests==1&&model.data(model.index(2),ResourceModel::Muted).toBool());
        require(QMetaObject::invokeMethod(disconnect,"clicked"));require(controller.disconnectRequests==1&&!model.selected(Kind::camera)&&!model.selected(Kind::display)&&!model.selected(Kind::microphone));
        controller.insert("active",false);model.setLocked(false);model.setSessionState(Kind::display,false,"Stopped");model.setSessionState(Kind::microphone,false,"Stopped");
        usb->setProperty("checked",true);require(QMetaObject::invokeMethod(usb,"clicked"));app.processEvents();require(connect->property("enabled").toBool());
        model.setDevices(Kind::usb,QVariantList{QVariantMap{{"label","Protected disk"},{"id","blocked"},{"available",false}},QVariantMap{{"label","Removable storage"},{"id","safe"},{"available",true}}},{},"Choose storage",false);
        model.setSelectedDevice(Kind::usb,"blocked");require(model.selectedDevice(Kind::usb).isEmpty());model.setSelectedDevice(Kind::usb,"safe");app.processEvents();require(connect->property("enabled").toBool());
        model.setSessionState(Kind::camera,false,"Stopped");
        const QString blockedId(64,'a'),releasedId(64,'b');controller.insert("usbRecoveries",QVariantList{QVariantMap{{"label","Bound"},{"id",blockedId},{"available",false}},QVariantMap{{"label","Released"},{"id",releasedId},{"available",true}}});app.processEvents();
        auto* recovery=named(picker,"usbRecoveryPicker");auto* restore=named(picker,"usbRecoveryRestoreButton");recovery->setProperty("currentIndex",0);app.processEvents();require(!restore->property("enabled").toBool());recovery->setProperty("currentIndex",1);app.processEvents();require(restore->property("enabled").toBool());require(QMetaObject::invokeMethod(restore,"clicked"));require(controller.recoveryRequests==1&&controller.recoveredId==releasedId);
        controller.insert("recoveryBusy",true);app.processEvents();require(!restore->property("enabled").toBool());controller.insert("recoveryBusy",false);controller.insert("hosting",true);app.processEvents();require(!restore->property("enabled").toBool());controller.insert("hosting",false);
        model.setDevices(Kind::camera,{},"cam-a","Camera unplugged",false);app.processEvents();require(connect->property("enabled").toBool()&&model.selectedDevice(Kind::microphone)=="mic-a");
        model.setCapability(Kind::camera,{false,"Camera unavailable"});app.processEvents();require(!model.selected(Kind::camera)&&connect->property("enabled").toBool());
        auto* window=qobject_cast<QWindow*>(root);require(window);window->resize(390,844);app.processEvents();window->resize(1120,760);app.processEvents();
        require(controller.setupRequests==0&&controller.recoveryRefreshRequests==0&&controller.ejectRequests==0&&!warnings);
        std::cout<<"Shared UI multi-resource model and offscreen shell checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#include "shared_ui_check.moc"
