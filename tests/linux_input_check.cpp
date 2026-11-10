#include "input.hpp"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QPointingDevice>
#include <QTest>
#include <QQmlEngine>
#include <QQmlComponent>
#include <stdexcept>
#include <iostream>
#include <memory>
using namespace dm::linux_client;
using namespace dm::input;
void need(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
class TabletObserver : public QObject {
public:
    int seen = 0;
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() == QEvent::TabletPress || e->type() == QEvent::TabletMove || e->type() == QEvent::TabletRelease)
            ++seen;
        return false;
    }
};
class Surface : public InputSurface {
public:
    using InputSurface::eventFilter;
    using InputSurface::focusOutEvent;
    using InputSurface::touchEvent;
};
int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents, false);
    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents, false);
    QGuiApplication app(argc, argv);
    try {
        QQuickWindow window;
        window.resize(640, 480);
        InputQueue queue;
        Surface surface;
        surface.setParentItem(window.contentItem());
        surface.setSize({640, 480});
        surface.sourceWidth = 640;
        surface.sourceHeight = 480;
        surface.queue = &queue;
        queue.grant(mouse_cap | keyboard_cap); // ungranted touch must never leave the receiver
        QPointingDevice touch("synthetic touchscreen", 1, QInputDevice::DeviceType::TouchScreen,
                              QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 10, 0);
        auto touchPacket = [&](QEvent::Type type, QEventPoint::State state) {
            QList<QEventPoint> points{QEventPoint(40, state, {100, 100}, {100, 100}),
                                      QEventPoint(900, state, {200, 100}, {200, 100})};
            QTouchEvent event(type, &touch, Qt::NoModifier, points);
            surface.touchEvent(&event);
        };
        touchPacket(QEvent::TouchBegin, QEventPoint::State::Pressed);
        need(queue.take().empty(), "Ungrant touch leaked");
        queue.grant(all_caps);
        touchPacket(QEvent::TouchBegin, QEventPoint::State::Pressed);
        auto down = queue.take();
        need(down.size() == 2 && down[0].kind == Kind::touch && down[0].id != down[1].id && down[0].flags == 3,
             "Multitouch down mapping");
        auto bytes = encode_events(down);
        need(decode_events(bytes, 2).size() == 2, "Touch wire validation");
        touchPacket(QEvent::TouchUpdate, QEventPoint::State::Updated);
        auto move = queue.take();
        need(move.size() == 2 && move[0].id == down[0].id && move[1].id == down[1].id, "Contact IDs changed");
        touchPacket(QEvent::TouchEnd, QEventPoint::State::Released);
        auto up = queue.take();
        need(up.size() == 2 && up[0].action == Action::up && !up[0].flags, "Touch release");
        encode_events(up);
        touchPacket(QEvent::TouchBegin, QEventPoint::State::Pressed);
        queue.take();
        QTouchEvent cancel(QEvent::TouchCancel, &touch);
        surface.touchEvent(&cancel);
        auto reset = queue.take();
        need(reset.size() == 1 && reset[0].kind == Kind::reset, "Touch cancellation cleanup");
        QPointingDevice pen(
            "synthetic eraser", 2, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Eraser,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure | QInputDevice::Capability::XTilt |
                QInputDevice::Capability::YTilt | QInputDevice::Capability::Rotation,
            1, 3);
        QTabletEvent press(QEvent::TabletPress, &pen, {320, 240}, {320, 240}, 0.5, 20, -30, 0, 45, 0, Qt::NoModifier,
                           Qt::LeftButton, Qt::LeftButton | Qt::RightButton);
        surface.eventFilter(&window, &press);
        auto events = queue.take();
        need(events.size() == 1, "Missing pen press");
        auto e = events[0];
        need(e.kind == Kind::pen && e.action == Action::down && e.pressure == 512 && e.tilt_x == 20 &&
                 e.tilt_y == -30 && e.rotation == 45 && e.mask == 15 && e.code == 7,
             "Pen axes, buttons and eraser mapping");
        encode_events(events);
        QFocusEvent focus(QEvent::FocusOut);
        surface.focusOutEvent(&focus);
        reset = queue.take();
        need(reset.size() == 1 && reset[0].kind == Kind::reset, "Focus-loss cleanup");
        queue.grant(mouse_cap);
        surface.eventFilter(&window, &press);
        need(queue.take().empty(), "Ungrant pen leaked");
        queue.grant(all_caps);
        QEvent proximity(QEvent::TabletLeaveProximity);
        surface.eventFilter(qGuiApp, &proximity);
        queue.take();
        surface.setExclusionRects({QRectF(0, 0, 640, 80)});
        QTabletEvent localPress(QEvent::TabletPress, &pen, {320, 40}, {320, 40}, 0.5, 0, 0, 0, 0, 0, Qt::NoModifier,
                                Qt::LeftButton, Qt::LeftButton);
        need(surface.eventFilter(&window, &localPress) && queue.take().empty(),
             "Local toolbar pen press was forwarded");
        QTabletEvent localDrag(QEvent::TabletMove, &pen, {320, 240}, {320, 240}, 0.5, 0, 0, 0, 0, 0, Qt::NoModifier,
                               Qt::NoButton, Qt::LeftButton);
        need(surface.eventFilter(&window, &localDrag) && queue.take().empty(),
             "Local pen gesture leaked after leaving toolbar");
        QTabletEvent localRelease(QEvent::TabletRelease, &pen, {320, 240}, {320, 240}, 0, 0, 0, 0, 0, 0, Qt::NoModifier,
                                  Qt::LeftButton, Qt::NoButton);
        need(surface.eventFilter(&window, &localRelease) && queue.take().empty(), "Local pen release was forwarded");
        surface.eventFilter(&window, &press);
        need(queue.take().size() == 1, "Remote pen did not resume after local gesture");
        Event heldKey;
        heldKey.kind = Kind::key;
        heldKey.action = Action::down;
        heldKey.id = 65;
        heldKey.code = 30;
        queue.append(heldKey);
        QTabletEvent cross(QEvent::TabletMove, &pen, {320, 40}, {320, 40}, 0.5, 0, 0, 0, 0, 0, Qt::NoModifier,
                           Qt::NoButton, Qt::LeftButton);
        need(!surface.eventFilter(&window, &cross), "Overlay crossing swallowed local tablet event");
        events = queue.take();
        need(events.size() == 2 && events[0].kind == Kind::key && events[1].kind == Kind::pen &&
                 events[1].action == Action::cancel,
             "Overlay crossing must cancel only the remote pen contact");
        encode_events(events);
        surface.eventFilter(&window, &localRelease);
        queue.take();
        surface.setExclusionRects({});
        surface.eventFilter(&window, &press);
        queue.take();
        surface.setExclusionRects({QRectF(200, 200, 200, 100)});
        events = queue.take();
        need(events.size() == 1 && events[0].kind == Kind::pen && events[0].action == Action::cancel,
             "New overlay did not release stationary pen contact");
        surface.eventFilter(&window, &localRelease);
        surface.setExclusionRects({});
        queue.take();
        TabletObserver observer;
        window.installEventFilter(&observer);
        surface.setExclusionRects({QRectF(0, 0, 640, 80)});
        QCoreApplication::sendEvent(&window, &localPress);
        QCoreApplication::sendEvent(&window, &localDrag);
        QCoreApplication::sendEvent(&window, &localRelease);
        need(observer.seen == 0 && queue.take().empty(), "Local synthesized pen sequence leaked original tablet input");
        window.removeEventFilter(&observer);
        surface.setExclusionRects({});
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport QtQuick.Controls\nButton { x:250; y:0; width:140; height:80; "
                          "property int taps: 0; onClicked: taps++ }",
                          QUrl());
        std::unique_ptr<QObject> buttonObject(component.create());
        auto* button = qobject_cast<QQuickItem*>(buttonObject.get());
        need(button, "Cannot create local toolbar button");
        button->setParentItem(window.contentItem());
        button->setZ(10);
        surface.setExclusionRects({QRectF(250, 0, 140, 80)});
        window.show();
        app.processEvents();
        queue.take();
        QPointingDevice localPen("synthetic local pen", 3, QInputDevice::DeviceType::Stylus,
                                 QPointingDevice::PointerType::Pen,
                                 QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        QTabletEvent buttonPress(QEvent::TabletPress, &localPen, {320, 40}, {320, 40}, 0.5, 0, 0, 0, 0, 0,
                                 Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
        QTabletEvent buttonRelease(QEvent::TabletRelease, &localPen, {320, 40}, {320, 40}, 0, 0, 0, 0, 0, 0,
                                   Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
        QCoreApplication::sendEvent(&window, &buttonPress);
        QCoreApplication::sendEvent(&window, &buttonRelease);
        app.processEvents();
        need(button->property("taps").toInt() == 1 && queue.take().empty(),
             "Pen tap must activate local QtQuick button without remote injection");
        QCoreApplication::sendEvent(&window, &press);
        queue.take();
        QCoreApplication::sendEvent(&window, &cross);
        QCoreApplication::sendEvent(&window, &buttonRelease);
        app.processEvents();
        need(button->property("taps").toInt() == 1, "Remote pen crossing overlay activated local button");
        queue.take();
        for (int cleanup = 0; cleanup < 3; ++cleanup) {
            QCoreApplication::sendEvent(&window, &buttonPress);
            app.processEvents();
            need(button->property("pressed").toBool(), "Local pen did not grab button");
            if (cleanup == 0)
                surface.resetInput();
            else if (cleanup == 1)
                surface.focusOutEvent(&focus);
            else
                surface.eventFilter(qGuiApp, &proximity);
            app.processEvents();
            need(!button->property("pressed").toBool(), "Local pen cleanup retained button grab");
            QCoreApplication::sendEvent(&window, &buttonRelease);
            app.processEvents();
            need(button->property("taps").toInt() == 1, "Canceled local pen gesture clicked button");
            queue.take();
        }
        surface.setEnabled(false);
        surface.setExclusionRects({});
        queue.take();
        QCoreApplication::sendEvent(&window, &buttonPress);
        QCoreApplication::sendEvent(&window, &buttonRelease);
        app.processEvents();
        need(button->property("taps").toInt() == 2 && queue.take().empty(),
             "Paused remote input must preserve local pen controls without injection");
        surface.setEnabled(true);
        buttonObject.reset();
        surface.setExclusionRects({});
        window.hide();
        queue.take();

        queue.grant(all_caps);
        Event key;
        key.kind = Kind::key;
        key.action = Action::down;
        key.id = 65;
        key.code = 30;
        for (unsigned i = 0; i < max_events + 1; i++) queue.append(key);
        need(queue.overflowed(), "Input overflow must fail closed");
        events = queue.take();
        need(events.size() == 1 && events[0].kind == Kind::reset, "Overflow releases input");
        std::cout << "Linux native touch/pen capture, capability gates, wire bounds and cleanup passed (synthetic "
                     "events; no physical-device claim).\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
