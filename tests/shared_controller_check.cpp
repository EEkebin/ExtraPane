#include "windows_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <source_location>
using Kind=extrapane::ResourceKind;
static void require(bool value,const std::source_location where=std::source_location::current()){
    if(!value)throw std::runtime_error("Controller check failed at line "+std::to_string(where.line()));
}
template<class F>static void until(F condition){
    QElapsedTimer timer;timer.start();
    while(!condition()&&timer.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(10);}
    require(condition());
}
static bool running(ResourceModel* model,int row){return model->data(model->index(row),ResourceModel::Running).toBool();}
static QString status(ResourceModel* model,int row){return model->data(model->index(row),ResourceModel::Status).toString();}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        WindowsController controller(nullptr,false);auto* model=controller.resources();model->select(0,false);
        require(controller.clientReady());
        controller.connectTo("192.0.2.1","0.0.0.0",false,0,0,43824,{});
        until([&]{return controller.status().startsWith("Connected.");});
        require(controller.active()&&!model->locked()&&!running(model,0));
        model->select(1,true);require(status(model,1).contains("Choose a source")&&!running(model,1));
        const QVariantList camera{QVariantMap{{"id","63616d"},{"label","Fake camera"},{"available",true}}};
        model->setDevices(Kind::camera,camera,{},"Ready",false);controller.selectDevice("camera","63616d");
        until([&]{return status(model,1).startsWith("Sharing camera");});require(running(model,1));
        // Re-check while graceful stop is pending; restart only after old child exits.
        model->select(1,false);model->select(1,true);
        until([&]{return status(model,1).startsWith("Sharing camera");});require(model->selected(Kind::camera));
        qputenv("EXTRAPANE_TEST_MIC_FAIL","1");
        model->setDevices(Kind::microphone,{QVariantMap{{"id","6d6963"},{"label","Fake mic"},{"available",true}}},"6d6963","Ready",false);
        model->select(2,true);
        until([&]{return !model->selected(Kind::microphone)&&status(model,2).contains("simulated microphone rejection");});
        require(controller.active()&&running(model,1));qunsetenv("EXTRAPANE_TEST_MIC_FAIL");
        // Failure is not automatically retried on unrelated model updates.
        model->setDevices(Kind::microphone,model->devices(Kind::microphone),"6d6963","Ready",false);
        QCoreApplication::processEvents();require(!running(model,2)&&!model->selected(Kind::microphone));
        model->select(2,true);until([&]{return status(model,2).startsWith("Sharing microphone");});
        model->select(1,false);model->select(2,false);
        until([&]{return !running(model,1)&&!running(model,2);});
        require(controller.active()); // no media still has a real control session
        model->select(1,true);model->select(2,true);
        until([&]{return status(model,1).startsWith("Sharing camera")&&status(model,2).startsWith("Sharing microphone");});
        controller.disconnectFromServer();
        require(!model->selected(Kind::camera)&&!model->selected(Kind::microphone)&&!model->selected(Kind::display)&&!model->selected(Kind::usb));
        until([&]{return !controller.active();});
        require(!running(model,1)&&!running(model,2)&&controller.clientReady());
        qputenv("EXTRAPANE_TEST_SESSION_EXIT","1");model->select(1,true);
        controller.connectTo("192.0.2.1","0.0.0.0",false,0,0,43824,{});
        until([&]{return status(model,1).startsWith("Sharing camera");});
        until([&]{return !controller.active();});
        require(!model->selected(Kind::camera)&&!running(model,1));qunsetenv("EXTRAPANE_TEST_SESSION_EXIT");
        qputenv("EXTRAPANE_TEST_BAD_TICKET","1");model->select(1,true);
        controller.connectTo("192.0.2.1","0.0.0.0",false,0,0,43824,{});
        until([&]{return !controller.active();});
        require(!model->selected(Kind::camera)&&!running(model,1)&&controller.status().contains("Invalid server session response"));qunsetenv("EXTRAPANE_TEST_BAD_TICKET");
        std::cout<<"Persistent Windows controller fake-child lifecycle checks passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
