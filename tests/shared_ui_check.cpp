#include "resource_model.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QWindow>
#include <QQuickStyle>
#include <QVariantList>
#include <iostream>
#include <stdexcept>

static void require(bool condition){if(!condition)throw std::runtime_error("Shared UI model check failed");}
class TestController final : public QQmlPropertyMap {
    Q_OBJECT
public:
    TestController():QQmlPropertyMap(this,nullptr){}
    Q_INVOKABLE void releaseInput(){}
};
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
        std::cout<<"Shared UI model and offscreen shell checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#include "shared_ui_check.moc"
