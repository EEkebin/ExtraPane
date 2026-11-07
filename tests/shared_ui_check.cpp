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
    int refreshRequests=0;
    int disconnectRequests=0;
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
        camera.insert("hostMode",QString("camera"));
        camera.insert("clientMode",QString("camera"));
        camera.insert("availability",QString{});
        camera.insert("cameraSetupRunning",false);
        camera.insert("cameras",QVariantList{});
        camera.insert("cameraScanRunning",false);
        camera.insert("cameraStatus",QString("No cameras found"));
        camera.insert("selectedCamera",QString{});
        QQmlApplicationEngine cameraEngine;
        QObject::connect(&cameraEngine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError>& errors){
            warnings=true;for(const auto& error:errors)std::cerr<<error.toString().toStdString()<<'\n';
        });
        cameraEngine.rootContext()->setContextProperty("receiver",&camera);
        cameraEngine.load(QUrl("qrc:/Main.qml"));
        require(!cameraEngine.rootObjects().isEmpty());
        app.processEvents();
        auto* cameraRoot=cameraEngine.rootObjects().first();
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
        cameraRoot->setProperty("page",0);app.processEvents();
        cameraRoot->setProperty("page",2);app.processEvents();
        cameraRoot->setProperty("page",3);app.processEvents();
        require(!warnings);
        std::cout<<"Shared UI model and offscreen shell checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#include "shared_ui_check.moc"
