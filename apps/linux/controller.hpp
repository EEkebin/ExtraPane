#pragma once
#include "input.hpp"
#include "media.hpp"
#include "receiver.hpp"
#include "host.hpp"
#include "resource_model.hpp"
#include <QObject>
#include <QQuickWindow>
#include <QVariantList>
#include <memory>
#include "connection_runtime.hpp"
class QQmlEngine;

class Controller final : public QObject {
    Q_OBJECT
    Q_PROPERTY(LinuxConnectionRuntime* connectionRuntime READ connectionRuntime CONSTANT)
    Q_PROPERTY(QString accessCode READ accessCode NOTIFY hostChanged)
    Q_PROPERTY(ResourceModel* resources READ resources CONSTANT)
    Q_PROPERTY(QString platformLabel READ platformLabel CONSTANT)
    Q_PROPERTY(QString hostDescription READ hostDescription CONSTANT)
    Q_PROPERTY(QString inputDescription READ inputDescription CONSTANT)
    Q_PROPERTY(QString rendererDescription READ rendererDescription CONSTANT)
    Q_PROPERTY(bool hosting READ hosting NOTIFY hostChanged)
    Q_PROPERTY(QString hostStatus READ hostStatus NOTIFY hostChanged)
    Q_PROPERTY(QString hostAvailability READ hostAvailability CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString availability READ availability CONSTANT)
    Q_PROPERTY(QString savedAddress READ savedAddress CONSTANT)
    Q_PROPERTY(QVariantList addresses READ addresses NOTIFY addressesChanged)
    Q_PROPERTY(QString statistics READ statistics NOTIFY statisticsChanged)
    Q_PROPERTY(QString decoder READ decoder NOTIFY statisticsChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(unsigned sourceWidth READ sourceWidth NOTIFY changed)
    Q_PROPERTY(unsigned sourceHeight READ sourceHeight NOTIFY changed)
public:
    explicit Controller(QString availability, QObject* parent = nullptr, bool multiClient = true);
    ~Controller() override;
    ResourceModel* resources() { return &resources_; }
    QString platformLabel() const { return "Linux"; }
    QString hostDescription() const {
        return "Clients choose displays, cameras and microphones on one server address and port. Display hosting "
               "requires a Wayland virtual-monitor portal; media endpoints require their platform integrations.";
    }
    QString inputDescription() const {
        return "Touch and pen are forwarded when granted by the server. Linux hosting supports mouse, keyboard and "
               "touch through the portal; pen injection is unavailable.";
    }
    QString rendererDescription() const {
        return "Video presentation: OpenGL. GStreamer selects the decoder from installed plugins. Hardware "
               "acceleration is not guaranteed.";
    }
    void setWindow(QQuickWindow* window) { window_ = window; }
    bool hosting() const { return host_ != nullptr; }
    QString hostStatus() const { return hostStatus_; }
    QString hostAvailability() const { return hostAvailability_; }
    QString accessCode() const { return accessCode_; }
    Q_INVOKABLE void generateAccessCode();
    Q_INVOKABLE void startHost(QString address, unsigned port = 43824, bool requireCode = false);
    Q_INVOKABLE void stopHost();
    bool active() const;
    LinuxConnectionRuntime* connectionRuntime() const { return runtime_.get(); }
    void setEngine(QQmlEngine* engine);
    void setDisplayMode(QVariantMap mode) { displayMode_ = std::move(mode); }
    void setScreenRequest(dm::video::wire::ScreenRequest screen, bool input = false) {
        screenRequest_ = screen;
        screenInput_ = input;
    }
    QString status() const { return status_; }
    QString availability() const { return availability_; }
    QString savedAddress() const;
    QVariantList addresses() const { return addresses_; }
    QString statistics() const { return statistics_; }
    QString decoder() const { return decoder_; }
    QString version() const;
    unsigned sourceWidth() const { return width_; }
    unsigned sourceHeight() const { return height_; }
    Q_INVOKABLE void refreshAddresses();
    Q_INVOKABLE void attachSurface(QObject* video, QObject* input);
    Q_INVOKABLE void connectTo(QString server, QString local, bool input, unsigned scale, unsigned interfaceIndex,
                               unsigned port = 43824, QString code = {});
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void releaseInput();
    void shutdown();
signals:
    void changed();
    void hostChanged();
    void addressesChanged();
    void statisticsChanged();

private:
    std::unique_ptr<LinuxConnectionRuntime> runtime_;
    QVariantMap displayMode_;
    dm::video::wire::ScreenRequest screenRequest_;
    bool screenInput_ = false;
    ResourceModel resources_;
    std::shared_ptr<dm::access::Gate> accessGate_;
    QString accessCode_;
    dm::linux_client::Host* host_ = nullptr;
    QString hostStatus_ = "Server stopped", hostAvailability_;
    dm::linux_client::InputQueue input_;
    dm::linux_client::Media media_;
    dm::linux_client::Receiver* receiver_ = nullptr;
    QQuickWindow* window_ = nullptr;
    QQuickItem* video_ = nullptr;
    dm::linux_client::InputSurface* inputSurface_ = nullptr;
    QString availability_, status_ = "Ready", statistics_, decoder_ = "Waiting for decoder";
    QVariantList addresses_;
    unsigned width_ = 1280, height_ = 720;
    bool failed_ = false;
};
