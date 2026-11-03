#pragma once
#include <QObject>
#include <QQuickItem>
#include <gst/gst.h>
#include <condition_variable>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace dm::linux_client {
class Media final : public QObject {
    Q_OBJECT
public:
    explicit Media(QObject* parent=nullptr);
    ~Media() override;
    void prepare(QQuickItem* item,unsigned width,unsigned height,unsigned serial);
    void stop();
    // Returned reference belongs to the receiver thread. No raw pixels cross Qt signals.
    GstElement* waitSource(unsigned serial,const std::function<bool()>& cancelled);
    void waitEos(const std::function<bool()>& cancelled,int seconds=10);
    void resetCounters(){decoded_=0;}
    std::uint64_t decodedFrames()const{return decoded_.load();}
signals:
    void failed(QString reason);
    void decoderChanged(QString name);
private:
    struct RenderState;
    std::shared_ptr<RenderState> render_;
    GstElement* pipeline_=nullptr;
    GstElement* source_=nullptr;
    GstBus* bus_=nullptr;
    std::mutex mutex_;
    std::condition_variable changed_;
    unsigned serial_=0,readySerial_=0;
    QString failure_;
    bool eos_=false;
    std::atomic_uint64_t decoded_{0};
    void inspectBus();
};
}
