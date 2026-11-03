#include "input.hpp"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>

namespace dm::linux_client {
using namespace dm::input;
void InputQueue::grant(unsigned caps){
    std::lock_guard lock(mutex_);events_.clear();caps_=caps;overflow_=false;
}
void InputQueue::append(Event event){
    std::lock_guard lock(mutex_);
    if(!caps_ || overflow_ || (capability(event.kind)&~caps_.load()))return;
    event.micros=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if(event.kind==Kind::mouse && event.action==Action::move && !events_.empty() &&
       events_.back().kind==Kind::mouse && events_.back().action==Action::move){events_.back()=event;return;}
    if(events_.size()>=max_events){events_.clear();events_.push_back(Event{});overflow_=true;return;}
    events_.push_back(event);
}
void InputQueue::reset(){std::lock_guard lock(mutex_);events_.clear();if(caps_)events_.push_back(Event{});}
std::vector<Event> InputQueue::take(){std::lock_guard lock(mutex_);std::vector<Event> result;result.swap(events_);return result;}
InputSurface::InputSurface(QQuickItem* parent):QQuickItem(parent){
    setAcceptedMouseButtons(Qt::AllButtons);setAcceptHoverEvents(true);
    // Do not emulate touch or pen as mouse input. Native families are a separate milestone.
    setAcceptTouchEvents(false);
}
void InputSurface::resetInput(){keys_.clear();buttons_=0;if(queue)queue->reset();}
bool InputSurface::point(QPointF p,Event& event,bool clamp) const {
    if(!queue || !(queue->capabilities()&mouse_cap) || width()<=0 || height()<=0 || sourceWidth<=0 || sourceHeight<=0)return false;
    const double scale=std::min(width()/sourceWidth,height()/sourceHeight);
    const double w=sourceWidth*scale,h=sourceHeight*scale,x=(width()-w)/2,y=(height()-h)/2;
    if(!clamp && (p.x()<x || p.y()<y || p.x()>=x+w || p.y()>=y+h))return false;
    event.x=static_cast<unsigned>(std::lround(std::clamp((p.x()-x)/w,0.0,1.0)*65535));
    event.y=static_cast<unsigned>(std::lround(std::clamp((p.y()-y)/h,0.0,1.0)*65535));
    return true;
}
void InputSurface::mouse(QMouseEvent* e,Action action){
    if(e->source()!=Qt::MouseEventNotSynthesized){e->accept();return;}
    Event event;event.kind=Kind::mouse;event.action=action;
    const unsigned button=e->button()==Qt::LeftButton?1:e->button()==Qt::RightButton?2:
        e->button()==Qt::MiddleButton?3:e->button()==Qt::BackButton?4:e->button()==Qt::ForwardButton?5:0;
    if(action!=Action::move){
        if(!button || (action==Action::up && !(buttons_&(1u<<button)))){e->ignore();return;}
        event.code=button;
    }
    if(!point(e->position(),event,buttons_!=0)){e->ignore();return;}
    if(action==Action::down){buttons_|=1u<<button;forceActiveFocus();}
    if(action==Action::up)buttons_&=~(1u<<button);
    queue->append(event);e->accept();
}
void InputSurface::mousePressEvent(QMouseEvent* e){mouse(e,Action::down);}
void InputSurface::mouseDoubleClickEvent(QMouseEvent* e){mouse(e,Action::down);}
void InputSurface::mouseReleaseEvent(QMouseEvent* e){mouse(e,Action::up);}
void InputSurface::mouseMoveEvent(QMouseEvent* e){mouse(e,Action::move);}
void InputSurface::hoverMoveEvent(QHoverEvent* e){Event event;event.kind=Kind::mouse;if(point(e->position(),event))queue->append(event);}
void InputSurface::wheelEvent(QWheelEvent* e){
    Event event;event.kind=Kind::mouse;event.action=Action::wheel;
    if(!point(e->position(),event)){e->ignore();return;}
    for(unsigned axis=0;axis<2;++axis){const int value=axis?e->angleDelta().x():e->angleDelta().y();
        if(value){event.code=axis;event.value=std::clamp(value,-32767,32767);queue->append(event);}}
    e->accept();
}
namespace {
struct Key {unsigned vk=0,scan=0;bool extended=false;};
Key physicalKey(quint32 native){
    // Qt's xcb/Wayland backends expose XKB keycodes (Linux evdev + 8).
    if(native<9)return {};
    const unsigned code=native-8;
    static constexpr unsigned vk[]={0,0x1b,'1','2','3','4','5','6','7','8','9','0',0xbd,0xbb,8,9,
        'Q','W','E','R','T','Y','U','I','O','P',0xdb,0xdd,13,0xa2,'A','S','D','F','G','H','J','K','L',
        0xba,0xde,0xc0,0xa0,0xdc,'Z','X','C','V','B','N','M',0xbc,0xbe,0xbf,0xa1,0x6a,0xa4,32,0x14,
        0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x90,0x91,0x67,0x68,0x69,0x6d,0x64,0x65,0x66,0x6b,0x61,0x62,0x63,0x60,0x6e};
    if(code<std::size(vk))return {vk[code],code,false};
    switch(code){
    case 86:return {0xe2,0x56,false};case 87:return {0x7a,0x57,false};case 88:return {0x7b,0x58,false};
    case 96:return {13,0x1c,true};case 97:return {0xa3,0x1d,true};case 98:return {0x6f,0x35,true};
    case 100:return {0xa5,0x38,true};case 102:return {0x24,0x47,true};case 103:return {0x26,0x48,true};
    case 104:return {0x21,0x49,true};case 105:return {0x25,0x4b,true};case 106:return {0x27,0x4d,true};
    case 107:return {0x23,0x4f,true};case 108:return {0x28,0x50,true};case 109:return {0x22,0x51,true};
    case 110:return {0x2d,0x52,true};case 111:return {0x2e,0x53,true};
    case 125:return {0x5b,0x5b,true};case 126:return {0x5c,0x5c,true};case 127:return {0x5d,0x5d,true};
    default:return {};
    }
}
}
void InputSurface::key(QKeyEvent* e,bool down){
    if(!queue || !(queue->capabilities()&keyboard_cap)){e->ignore();return;}
    // Reserved shortcuts are handled locally and must never leave held modifiers remotely.
    if(e->key()==Qt::Key_F8 || e->key()==Qt::Key_F10 || e->key()==Qt::Key_F11 || (e->key()==Qt::Key_Q && e->modifiers().testFlag(Qt::ControlModifier) && e->modifiers().testFlag(Qt::ShiftModifier))){resetInput();e->ignore();return;}
    const Key code=physicalKey(e->nativeScanCode());
    if(!code.vk || (e->isAutoRepeat() && (!down || !keys_.contains(code.vk)))){e->accept();return;}
    if(down)keys_.insert(code.vk);else if(!keys_.erase(code.vk)){e->accept();return;}
    Event event;event.kind=Kind::key;event.action=down?Action::down:Action::up;
    event.id=code.vk;event.code=code.scan;event.value=code.extended?1:0;queue->append(event);e->accept();
}
void InputSurface::keyPressEvent(QKeyEvent* e){key(e,true);}
void InputSurface::keyReleaseEvent(QKeyEvent* e){key(e,false);}
void InputSurface::focusOutEvent(QFocusEvent* e){resetInput();QQuickItem::focusOutEvent(e);}
void InputSurface::mouseUngrabEvent(){if(buttons_)resetInput();}
}
