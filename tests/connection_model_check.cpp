#include "connection_model.hpp"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
#include <source_location>
static void require(bool condition,const std::source_location where=std::source_location::current()){if(!condition)throw std::runtime_error("Connection model check failed at "+std::to_string(where.line()));}
static QVariantMap target(QString address){return {{"address",address},{"port",43824},{"localAddress","0.0.0.0"},{"interfaceIndex",0}};}
static QVariantMap custom(int width=2560,int height=1440,int fps=60){return {{"mode","custom"},{"monitorId",""},{"width",width},{"height",height},{"fps",fps}};}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    ConnectionModel many;for(int i=0;i<160;++i)require(!many.addConnection("camera").isEmpty());require(many.rowCount()==160);
    ConnectionModel model;const auto camera=model.addConnection("camera"),second=model.addConnection("camera"),mic=model.addConnection("microphone");
    require(camera!=second&&!camera.isEmpty()&&model.rowCount()==3&&model.addConnection("invalid").isEmpty());
    require(model.setSharedTarget(target("192.0.2.10"))&&model.setTarget(camera,target("192.0.2.11")));
    require(model.setSource(camera,"camera-a")&&model.setSource(second,"camera-a"));model.setEnabled(camera,true);model.setEnabled(second,true);
    require(model.beginConnection(camera)&&model.beginConnection(second)); // Sharing same capture is a native gate, not a model singleton.
    require(model.activeConnectionCount()==2&&model.hasReservedResources());
    require(!model.removeConnection(camera)&&!model.setSource(camera,"camera-b")&&!model.setTarget(camera,target("192.0.2.12")));
    model.setSingleServer(false);require(model.connection(camera)["effectiveTarget"].toMap()==target("192.0.2.10"));
    require(model.connection(camera)["destinationVisible"].toBool());
    require(model.setSharedTarget(target("192.0.2.20")));require(model.connection(second)["effectiveTarget"].toMap()==target("192.0.2.10"));
    model.setState(camera,ConnectionModel::State::idle);require(model.connection(camera)["effectiveTarget"].toMap()==target("192.0.2.11"));
    require(model.removeConnection(camera));require(model.connection(second)["connectionId"]==second&&model.connection(mic)["connectionId"]==mic);
    auto secret=target("192.0.2.10");secret["accessCode"]="123456";require(!model.setSharedTarget(secret)&&!model.setTarget(mic,secret));
    require(!model.setSharedTarget(target("example.org"))&&!model.setSharedTarget(target("0.0.0.0"))&&!model.setSharedTarget(target("224.0.0.1")));
    auto bad=target("192.0.2.10");bad["port"]=65536;require(!model.setTarget(mic,bad));bad["port"]=1.5;require(!model.setTarget(mic,bad));
    const auto usb=model.addConnection("usb"),usb2=model.addConnection("usb");model.setSource(usb,"disk-a");model.setSource(usb2,"disk-a");model.setEnabled(usb,true);model.setEnabled(usb2,true);
    require(model.beginConnection(usb)&&!model.beginConnection(usb2));model.setEnabled(usb,false);model.setState(usb,ConnectionModel::State::returning,"Returning");require(!model.beginConnection(usb2));
    model.setState(usb,ConnectionModel::State::recovery,"Recovery required");require(!model.removeConnection(usb)&&!model.beginConnection(usb2));
    model.setState(usb,ConnectionModel::State::idle,"Returned");require(model.beginConnection(usb2));
    const auto display=model.addConnection("display");model.setEnabled(display,true);require(model.connection(display)["displayMode"].toMap()["mode"]=="custom"&&model.connection(display)["displayMode"].toMap()["monitorId"].toString().isEmpty());
    require(model.beginConnection(display));model.setState(display,ConnectionModel::State::idle);
    auto native=custom();native["mode"]="native";require(model.setDisplayMode(display,native)&&!model.beginConnection(display));
    auto stale=custom();stale["monitorId"]="old-monitor";require(model.setDisplayMode(display,stale)&&model.connection(display)["displayMode"].toMap()["monitorId"].toString().isEmpty());
    require(model.setDisplayMode(display,custom())&&!model.setDisplayMode(mic,custom()));
    require(!model.setDisplayMode(display,custom(0))&&!model.setDisplayMode(display,custom(2560,1440,0)));
    require(!model.setDisplayMode(display,custom(4096,4096,60))&&!model.setDisplayMode(display,custom(2561,1440,60))&&!model.setDisplayMode(display,custom(2560,1440,361)));
    require(model.setDisplayMode(display,custom(1920,1080,360)));
    require(model.connection(display)["displayMode"].toMap()["fps"].toInt()==360);
    require(model.beginConnection(display));require(!model.setDisplayMode(display,custom(1920,1080,60)));
    require(!model.setState(mic,ConnectionModel::State::active));
    require(!model.setState(display,ConnectionModel::State::recovery));
    std::cout<<"Connection model identity, frozen targets, validation and USB exclusivity passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
