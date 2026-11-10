#pragma once
#include "resource_model.hpp"
#include "windows_connection_runtime.hpp"
#include <QObject>
#include <QVariantList>
#include <memory>
#include <array>
#include <string>

class WindowsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(WindowsConnectionRuntime* connectionRuntime READ connectionRuntime CONSTANT)
    Q_PROPERTY(ResourceModel* resources READ resources CONSTANT)
    Q_PROPERTY(QString accessCode READ accessCode NOTIFY hostChanged)
    Q_PROPERTY(bool hosting READ hosting NOTIFY hostChanged)
    Q_PROPERTY(QString hostStatus READ hostStatus NOTIFY hostChanged)
    Q_PROPERTY(QString hostAvailability READ hostAvailability NOTIFY hostChanged)
    Q_PROPERTY(QVariantList hostComponents READ hostComponents NOTIFY hostChanged)
    Q_PROPERTY(bool cameraSetupRunning READ cameraSetupRunning NOTIFY hostChanged)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString availability READ availability NOTIFY changed)
    Q_PROPERTY(bool clientReady READ clientReady NOTIFY changed)
    Q_PROPERTY(QVariantList usbRecoveries READ usbRecoveries NOTIFY recoveryChanged)
    Q_PROPERTY(QString recoveryStatus READ recoveryStatus NOTIFY recoveryChanged)
    Q_PROPERTY(bool recoveryBusy READ recoveryBusy NOTIFY recoveryChanged)
    Q_PROPERTY(bool microphoneMuted READ microphoneMuted NOTIFY changed)
    Q_PROPERTY(bool cameraMirrored READ cameraMirrored NOTIFY changed)
    Q_PROPERTY(QString savedAddress READ savedAddress CONSTANT)
    Q_PROPERTY(QVariantList addresses READ addresses NOTIFY addressesChanged)
    Q_PROPERTY(QString statistics READ statistics NOTIFY changed)
    Q_PROPERTY(QString decoder READ decoder CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(unsigned sourceWidth READ sourceWidth CONSTANT)
    Q_PROPERTY(unsigned sourceHeight READ sourceHeight CONSTANT)
    Q_PROPERTY(QString platformLabel READ platformLabel CONSTANT)
    Q_PROPERTY(QString hostDescription READ hostDescription NOTIFY hostChanged)
    Q_PROPERTY(QString inputDescription READ inputDescription CONSTANT)
    Q_PROPERTY(QString rendererDescription READ rendererDescription CONSTANT)
public:
    explicit WindowsController(QObject* parent = nullptr, bool rememberAddress = true);
    ~WindowsController() override;
    ResourceModel* resources() { return &resources_; }
    WindowsConnectionRuntime* connectionRuntime() { return connectionRuntime_.get(); }
    QString accessCode() const { return code_; }
    bool hosting() const;
    bool active() const;
    QString hostStatus() const { return hostStatus_; }
    QString status() const { return status_; }
    QString availability() const;
    bool clientReady() const;
    Q_INVOKABLE void refreshDevices(QString kind);
    Q_INVOKABLE void selectDevice(QString kind, QString id);
    Q_INVOKABLE void stopResource(QString kind);
    bool microphoneMuted() const { return microphoneMuted_; }
    Q_INVOKABLE void setMicrophoneMuted(bool muted);
    bool cameraMirrored() const { return cameraMirrored_; }
    Q_INVOKABLE void setCameraMirrored(bool mirrored);
    Q_INVOKABLE void ejectUsb();
    QVariantList usbRecoveries() const { return recoveries_; }
    QString recoveryStatus() const { return recoveryStatus_; }
    bool recoveryBusy() const;
    Q_INVOKABLE void refreshUsbRecovery();
    Q_INVOKABLE void recoverUsb(QString id);
    QString hostAvailability() const;
    QVariantList hostComponents() const;
    bool cameraSetupRunning() const;
    QString savedAddress() const;
    QVariantList addresses() const { return addresses_; }
    QString statistics() const { return {}; }
    QString decoder() const { return "Native Windows receiver"; }
    QString version() const;
    unsigned sourceWidth() const { return 1280; }
    unsigned sourceHeight() const { return 720; }
    QString platformLabel() const { return "Windows"; }
    QString hostDescription() const;
    QString inputDescription() const {
        return "Mouse, keyboard, touch and pen use the native Windows receiver. Its window contains fullscreen and "
               "input controls.";
    }
    QString rendererDescription() const {
        return "The native receiver uses the saved Windows renderer preference. Its statistics are available in the "
               "receiver toolbar.";
    }
    Q_INVOKABLE void refreshAddresses();
    Q_INVOKABLE void connectTo(QString server, QString local, bool input, unsigned scale, unsigned interfaceIndex,
                               unsigned port, QString code);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void startHost(QString address, unsigned port, bool requireCode);
    Q_INVOKABLE void stopHost();
    Q_INVOKABLE void generateAccessCode();
    Q_INVOKABLE void releaseInput() {}
    void shutdown();
signals:
    void recoveryChanged();
    void changed();
    void hostChanged();
    void addressesChanged();

private:
    void poll();
    void prepareHostComponent();
    void failHostPreparation(QString reason);
    void launchHost();
    void startClients(std::array<std::string, 4> credentials);
    void cancelAdmission();
    void reconcileResources();
    QString resourceAvailability(const QString& kind) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ResourceModel resources_;
    std::unique_ptr<WindowsConnectionRuntime> connectionRuntime_;
    QVariantList addresses_;
    QString code_, hostStatus_ = "Server stopped", status_ = "Ready", unavailable_, hostUnavailable_;
    QVariantList recoveries_;
    QString recoveryStatus_ =
        "Refresh recovery records after USB stops. Recovery never forces a busy or unverified disk online.";
    bool microphoneMuted_ = false, cameraMirrored_ = false, rememberAddress_ = true;
};
