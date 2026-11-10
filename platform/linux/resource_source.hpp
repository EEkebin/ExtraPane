#pragma once
#include <QThread>
#include <QVariantList>
#include <atomic>

namespace dm::linux_client {
// Native capture and encrypted resource transport. No pixels/PCM cross QML.
class ResourceSource final : public QThread {
    Q_OBJECT
public:
    enum class Kind { Camera, Microphone };
    struct Options {
        Kind kind = Kind::Camera;
        QString device, server, local = "0.0.0.0", code;
        unsigned port = 43824, interfaceIndex = 0, width = 0, height = 0, fps = 0;
    };
    explicit ResourceSource(Options options, QObject* parent = nullptr);
    ~ResourceSource() override;
    static QVariantList devices(Kind kind);
    static QString unavailable(Kind kind);
    void setMuted(bool value) { muted_.store(value); }
    void setMirrored(bool value) { mirrored_.store(value); }
signals:
    void state(QString detail);
    void connected(unsigned width, unsigned height, unsigned fps);
    void failed(QString detail);

protected:
    void run() override;

private:
    Options options_;
    std::atomic_bool muted_ = false, mirrored_ = false;
};
}
