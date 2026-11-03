#include "input.hpp"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QPointingDevice>
#include <QTest>
#include <stdexcept>
#include <iostream>
using namespace dm::linux_client;
using namespace dm::input;
void need(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
class Surface:public InputSurface {public:using InputSurface::touchEvent;using InputSurface::eventFilter;using InputSurface::focusOutEvent;};
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);
    try{
        QQuickWindow window;window.resize(640,480);
        InputQueue queue;Surface surface;surface.setParentItem(window.contentItem());surface.setSize({640,480});surface.sourceWidth=640;surface.sourceHeight=480;surface.queue=&queue;
        queue.grant(mouse_cap|keyboard_cap); // ungranted touch must never leave the receiver
        QPointingDevice touch("synthetic touchscreen",1,QInputDevice::DeviceType::TouchScreen,QPointingDevice::PointerType::Finger,QInputDevice::Capability::Position,10,0);
        auto touchPacket=[&](QEvent::Type type,QEventPoint::State state){
            QList<QEventPoint> points{QEventPoint(40,state,{100,100},{100,100}),QEventPoint(900,state,{200,100},{200,100})};
            QTouchEvent event(type,&touch,Qt::NoModifier,points);surface.touchEvent(&event);
        };
        touchPacket(QEvent::TouchBegin,QEventPoint::State::Pressed);need(queue.take().empty(),"Ungrant touch leaked");
        queue.grant(all_caps);touchPacket(QEvent::TouchBegin,QEventPoint::State::Pressed);
        auto down=queue.take();need(down.size()==2&&down[0].kind==Kind::touch&&down[0].id!=down[1].id&&down[0].flags==3,"Multitouch down mapping");
        auto bytes=encode_events(down);need(decode_events(bytes,2).size()==2,"Touch wire validation");
        touchPacket(QEvent::TouchUpdate,QEventPoint::State::Updated);auto move=queue.take();need(move.size()==2&&move[0].id==down[0].id&&move[1].id==down[1].id,"Contact IDs changed");
        touchPacket(QEvent::TouchEnd,QEventPoint::State::Released);auto up=queue.take();need(up.size()==2&&up[0].action==Action::up&&!up[0].flags,"Touch release");encode_events(up);
        touchPacket(QEvent::TouchBegin,QEventPoint::State::Pressed);queue.take();
        QTouchEvent cancel(QEvent::TouchCancel,&touch);surface.touchEvent(&cancel);auto reset=queue.take();need(reset.size()==1&&reset[0].kind==Kind::reset,"Touch cancellation cleanup");
        QPointingDevice pen("synthetic eraser",2,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Eraser,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure|QInputDevice::Capability::XTilt|QInputDevice::Capability::YTilt|QInputDevice::Capability::Rotation,1,3);
        QTabletEvent press(QEvent::TabletPress,&pen,{320,240},{320,240},0.5,20,-30,0,45,0,Qt::NoModifier,Qt::LeftButton,Qt::LeftButton|Qt::RightButton);
        surface.eventFilter(&window,&press);auto events=queue.take();need(events.size()==1,"Missing pen press");auto e=events[0];
        need(e.kind==Kind::pen&&e.action==Action::down&&e.pressure==512&&e.tilt_x==20&&e.tilt_y==-30&&e.rotation==45&&e.mask==15&&e.code==7,"Pen axes, buttons and eraser mapping");encode_events(events);
        QFocusEvent focus(QEvent::FocusOut);surface.focusOutEvent(&focus);reset=queue.take();need(reset.size()==1&&reset[0].kind==Kind::reset,"Focus-loss cleanup");
        queue.grant(mouse_cap);surface.eventFilter(&window,&press);need(queue.take().empty(),"Ungrant pen leaked");
        queue.grant(all_caps);Event key;key.kind=Kind::key;key.action=Action::down;key.id=65;key.code=30;
        for(unsigned i=0;i<max_events+1;i++)queue.append(key);
        need(queue.overflowed(),"Input overflow must fail closed");events=queue.take();need(events.size()==1&&events[0].kind==Kind::reset,"Overflow releases input");
        std::cout<<"Linux native touch/pen capture, capability gates, wire bounds and cleanup passed (synthetic events; no physical-device claim).\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
