#include "windows_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QFile>
#include <iostream>
#include <stdexcept>
#include <source_location>
using Kind=extrapane::ResourceKind;
static void require(bool value,const std::source_location where=std::source_location::current()){
    if(!value)throw std::runtime_error("Controller check failed at line "+std::to_string(where.line()));
}
template<class F>static void until(F condition,const std::source_location where=std::source_location::current()){
    QElapsedTimer timer;timer.start();
    while(!condition()&&timer.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(10);}
    require(condition(),where);
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
        // USB graceful return: veto keeps the control session and USB selection.
        qputenv("EXTRAPANE_TEST_USB_BUSY","1");
        model->setDevices(Kind::usb,{QVariantMap{{"id","757362"},{"label","Fake removable disk"},{"available",true}}},"757362","Ready",false);
        controller.connectTo("192.0.2.1","0.0.0.0",false,0,0,43824,{});
        until([&]{return controller.status().startsWith("Connected.");});
        model->select(3,true);model->select(1,true);
        until([&]{return status(model,3).startsWith("USB storage is shared")&&running(model,1);});
        controller.disconnectFromServer();require(controller.active());
        until([&]{return model->selected(Kind::usb)&&status(model,3).startsWith("USB is still shared");});
        until([&]{return !running(model,1);});
        require(controller.active()&&running(model,3)&&!model->selected(Kind::camera));
        // Retry succeeds without restarting either process or dropping admission early.
        controller.disconnectFromServer();require(controller.active());
        until([&]{return !controller.active();});
        require(!model->selected(Kind::usb)&&status(model,3)=="USB returned to this PC.");
        controller.connectTo("192.0.2.1","0.0.0.0",false,0,0,43824,{});
        until([&]{return controller.status().startsWith("Connected.");});
        model->select(3,true);until([&]{return status(model,3).startsWith("USB storage is shared");});
        model->select(3,false);
        until([&]{return model->selected(Kind::usb)&&status(model,3).startsWith("USB is still shared");});
        require(controller.active()&&running(model,3));
        model->select(3,false);
        until([&]{return !running(model,3);});
        qunsetenv("EXTRAPANE_TEST_USB_BUSY");
        require(controller.active()&&status(model,3)=="USB returned to this PC.");
        until([&]{return !model->scanning(Kind::usb);});
        require(model->devices(Kind::usb).size()==1&&model->devices(Kind::usb)[0].toMap()["label"]=="Fake returned disk");
        controller.disconnectFromServer();until([&]{return !controller.active();});
        qputenv("EXTRAPANE_TEST_USB_NOT_READY","1");model->select(3,true);
        controller.connectTo("192.0.2.1","0.0.0.0",false,0,0,43824,{});
        until([&]{return status(model,3).startsWith("USB is not available");});
        require(!status(model,3).contains("shared"));
        controller.disconnectFromServer();
        until([&]{return model->selected(Kind::usb)&&status(model,3).startsWith("USB could not be returned safely");});
        require(controller.active()&&!status(model,3).contains("close its files"));
        QFile usbLog(QCoreApplication::applicationDirPath()+"/usb-diagnostics/usb.log");require(usbLog.open(QIODevice::ReadOnly));
        const auto diagnostics=usbLog.readAll();require(diagnostics.contains("USB_RETURN_FAILED")&&!diagnostics.contains("123456"));
        controller.disconnectFromServer();until([&]{return !controller.active();});
        qunsetenv("EXTRAPANE_TEST_USB_NOT_READY");
        qputenv("EXTRAPANE_TEST_SESSION_EXIT","1");model->select(1,true);
        controller.connectTo("192.0.2.1","0.0.0.0",false,0,0,43824,{});
        until([&]{return status(model,1).startsWith("Sharing camera");});
        until([&]{return !controller.active();});
        require(!model->selected(Kind::camera)&&!running(model,1));qunsetenv("EXTRAPANE_TEST_SESSION_EXIT");
        qputenv("EXTRAPANE_TEST_SESSION_EXIT","1");qputenv("EXTRAPANE_TEST_USB_BUSY","1");model->select(3,true);
        controller.connectTo("192.0.2.1","0.0.0.0",false,0,0,43824,{});
        until([&]{return !controller.active();});
        require(!model->selected(Kind::usb)&&!running(model,3)&&status(model,3).contains("simulated forced session loss"));
        qunsetenv("EXTRAPANE_TEST_SESSION_EXIT");qunsetenv("EXTRAPANE_TEST_USB_BUSY");
        qputenv("EXTRAPANE_TEST_BAD_TICKET","1");model->select(1,true);
        controller.connectTo("192.0.2.1","0.0.0.0",false,0,0,43824,{});
        until([&]{return !controller.active();});
        require(!model->selected(Kind::camera)&&!running(model,1)&&controller.status().contains("Invalid server session response"));qunsetenv("EXTRAPANE_TEST_BAD_TICKET");
        // Actual instance runtime with fake native capture brokers and no sockets.
        auto* runtime=controller.connectionRuntime();auto* connections=runtime->connections();
        const QVariantMap firstTarget{{"address","192.0.2.1"},{"port",43824},{"localAddress","0.0.0.0"},{"interfaceIndex",0}};
        require(connections->setSharedTarget(firstTarget));runtime->connectShared({});until([&]{return runtime->status().startsWith("Connected.");});require(controller.active());
        const auto cam1=connections->addConnection("camera"),cam2=connections->addConnection("camera"),mic1=connections->addConnection("microphone");
        connections->setSource(cam1,"63616d");connections->setSource(cam2,"63616d");connections->setSource(mic1,"6d6963");
        runtime->toggle(cam1,true);runtime->toggle(cam2,true);runtime->toggle(mic1,true);
        try{until([&]{return connections->connection(cam1)["phase"]=="active"&&connections->connection(cam2)["phase"]=="active"&&connections->connection(mic1)["phase"]=="active";});}catch(...){for(const auto& id:{cam1,cam2,mic1})std::cerr<<connections->connection(id)["phase"].toString().toStdString()<<" "<<connections->connection(id)["status"].toString().toStdString()<<"\n";std::cerr<<runtime->status().toStdString()<<"\n";throw;}
        runtime->toggle(cam1,false);until([&]{return connections->connection(cam1)["phase"]=="idle";});require(connections->connection(cam2)["phase"]=="active");
        runtime->setMirror(cam2,true);until([&]{return runtime->mirrorStates()["63616d"].toBool();});
        connections->setSingleServer(false);const auto bad=connections->addConnection("camera");auto secondTarget=firstTarget;secondTarget["address"]="192.0.2.2";connections->setTarget(bad,secondTarget);connections->setSource(bad,"626164");runtime->toggle(bad,true);
        until([&]{return connections->connection(bad)["phase"]=="failed";});require(connections->connection(cam2)["phase"]=="active"&&connections->connection(mic1)["phase"]=="active");
        runtime->disconnectAll();until([&]{return !runtime->active();});require(!controller.active());
        qputenv("EXTRAPANE_TEST_USB_BUSY","1");
        const auto storage=connections->addConnection("usb"),storage2=connections->addConnection("usb");connections->setSource(storage,"757362");connections->setSource(storage2,"757362");runtime->toggle(storage,true);
        until([&]{return connections->connection(storage)["phase"]=="active";});runtime->toggle(storage2,true);require(connections->connection(storage2)["phase"]!="active"&&connections->connection(storage2)["status"].toString().contains("reserved"));
        runtime->disconnectAll();until([&]{return connections->connection(storage)["connectionEnabled"].toBool()&&connections->connection(storage)["status"].toString().contains("busy");});require(runtime->active());
        runtime->disconnectAll();until([&]{return !runtime->active();});require(connections->connection(storage)["status"].toString().contains("returned"));qunsetenv("EXTRAPANE_TEST_USB_BUSY");
        std::cout<<"Persistent Windows controller fake-child lifecycle checks passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
