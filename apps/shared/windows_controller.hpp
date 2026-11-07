#pragma once
#include "resource_model.hpp"
#include <QObject>
#include <QVariantList>
#include <memory>

class WindowsController final:public QObject {
    Q_OBJECT
    Q_PROPERTY(ResourceModel* resources READ resources CONSTANT)
    Q_PROPERTY(QString accessCode READ accessCode NOTIFY hostChanged)
    Q_PROPERTY(bool hosting READ hosting NOTIFY hostChanged)
    Q_PROPERTY(QString hostStatus READ hostStatus NOTIFY hostChanged)
    Q_PROPERTY(QString hostAvailability READ hostAvailability NOTIFY hostChanged)
    Q_PROPERTY(QString hostMode READ hostMode WRITE setHostMode NOTIFY hostChanged)
    Q_PROPERTY(bool cameraSetupRunning READ cameraSetupRunning NOTIFY hostChanged)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString availability READ availability NOTIFY changed)
    Q_PROPERTY(QString clientMode READ clientMode WRITE setClientMode NOTIFY changed)
    Q_PROPERTY(QVariantList cameras READ cameras NOTIFY camerasChanged)
    Q_PROPERTY(bool cameraScanRunning READ cameraScanRunning NOTIFY camerasChanged)
    Q_PROPERTY(QString cameraStatus READ cameraStatus NOTIFY camerasChanged)
    Q_PROPERTY(QString selectedCamera READ selectedCamera WRITE setSelectedCamera NOTIFY camerasChanged)
    Q_PROPERTY(bool microphoneMuted READ microphoneMuted NOTIFY changed)
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
    explicit WindowsController(QObject* parent=nullptr);
    ~WindowsController() override;
    ResourceModel* resources(){return &resources_;}
    QString accessCode()const{return code_;}
    bool hosting()const;
    bool active()const;
    QString hostStatus()const{return hostStatus_;}
    QString status()const{return status_;}
    QString availability()const;
    QString clientMode()const{return clientMode_;}
    void setClientMode(QString mode);
    QVariantList cameras()const{return cameras_;}
    bool cameraScanRunning()const;
    QString cameraStatus()const{return cameraStatus_;}
    QString selectedCamera()const{return selectedCamera_;}
    void setSelectedCamera(QString id);
    Q_INVOKABLE void refreshCameras();
    bool microphoneMuted()const{return microphoneMuted_;}
    Q_INVOKABLE void setMicrophoneMuted(bool muted);
    Q_INVOKABLE void setupMicrophone();
    QString hostAvailability()const;
    QString hostMode()const{return hostMode_;}
    void setHostMode(QString mode);
    bool cameraSetupRunning()const;
    QString savedAddress()const;
    QVariantList addresses()const{return addresses_;}
    QString statistics()const{return {};}
    QString decoder()const{return "Native Windows receiver";}
    QString version()const;
    unsigned sourceWidth()const{return 1280;}
    unsigned sourceHeight()const{return 720;}
    QString platformLabel()const{return "Windows";}
    QString hostDescription()const;
    QString inputDescription()const{return "Mouse, keyboard, touch and pen use the native Windows receiver. Its window contains fullscreen and input controls.";}
    QString rendererDescription()const{return "The native receiver uses the saved Windows renderer preference. Its statistics are available in the receiver toolbar.";}
    Q_INVOKABLE void refreshAddresses();
    Q_INVOKABLE void connectTo(QString server,QString local,bool input,unsigned scale,unsigned interfaceIndex,unsigned port,QString code);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void startHost(QString address,unsigned port,bool requireCode);
    Q_INVOKABLE void stopHost();
    Q_INVOKABLE void generateAccessCode();
    Q_INVOKABLE void setupCamera();
    Q_INVOKABLE void releaseInput(){}
    void shutdown();
signals:
    void changed();void hostChanged();void addressesChanged();void camerasChanged();
private:
    void poll();
    struct Impl;std::unique_ptr<Impl> impl_;
    ResourceModel resources_;
    QVariantList addresses_;
    QString code_,hostStatus_="Server stopped",status_="Ready",unavailable_,hostUnavailable_;
    QString hostMode_="display",clientMode_="display",selectedCamera_,cameraStatus_="Click Refresh cameras to list local cameras. Capture starts only after connection is accepted.";
    QVariantList cameras_;
    bool microphoneMuted_=false;
};
