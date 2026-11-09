#pragma once
#include <dm/input_protocol.hpp>
#include <QQuickItem>
#include <QVariantList>
#include <QRectF>
#include <atomic>
#include <mutex>
#include <set>
#include <map>

namespace dm::linux_client {
class InputQueue final {
public:
    void grant(unsigned caps);
    void append(dm::input::Event event);
    void reset();
    std::vector<dm::input::Event> take();
    bool overflowed() const {return overflow_.load();}
    unsigned capabilities() const {return caps_.load();}
private:
    std::atomic_uint caps_{0};
    std::atomic_bool overflow_{false};
    std::mutex mutex_;
    std::vector<dm::input::Event> events_;
};

class InputSurface : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList exclusionRects READ exclusionRects WRITE setExclusionRects NOTIFY exclusionRectsChanged)
    Q_PROPERTY(int sourceWidth MEMBER sourceWidth NOTIFY geometryChanged)
    Q_PROPERTY(int sourceHeight MEMBER sourceHeight NOTIFY geometryChanged)
public:
    explicit InputSurface(QQuickItem* parent=nullptr);
    InputQueue* queue=nullptr;
    int sourceWidth=1280,sourceHeight=720;
    Q_INVOKABLE void resetInput();
    QVariantList exclusionRects()const{return exclusionRects_;}
    void setExclusionRects(QVariantList rectangles);
signals:
    void geometryChanged();
    void exclusionRectsChanged();
protected:
    bool eventFilter(QObject*, QEvent*) override;
    void touchEvent(QTouchEvent*) override;
    void touchUngrabEvent() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void hoverMoveEvent(QHoverEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void mouseUngrabEvent() override;
private:
    std::set<unsigned> keys_;
    std::map<int,unsigned> touches_;
    bool penContact_=false,localPenSequence_=false,localPenMouseDown_=false,dispatchingLocalPen_=false;
    QPointF lastPenScene_,lastPenGlobal_;
    Qt::KeyboardModifiers lastPenModifiers_=Qt::NoModifier;
    QVariantList exclusionRects_;
    bool excluded(QPointF scenePosition)const;
    void cancelRemotePen();
    void localPenMouse(QEvent::Type type,QPointF scene,QPointF global,Qt::MouseButton button,Qt::MouseButtons buttons);
    void releaseLocalPenMouse();
    unsigned nextTouch_=1;
    void tablet(QTabletEvent*);
    unsigned buttons_=0;
    bool point(QPointF position,dm::input::Event& event,bool clamp=false) const;
    void mouse(QMouseEvent*,dm::input::Action);
    void key(QKeyEvent*,bool down);
};
}
