#pragma once
#include "connection_model.hpp"
#include <QObject>
#include <memory>
#include <QVariantList>

class WindowsConnectionRuntime final:public QObject {
    Q_OBJECT
    Q_PROPERTY(ConnectionModel* connections READ connections CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool speakersSupported READ speakersSupported CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantMap mirrorStates READ mirrorStates NOTIFY changed)
    Q_PROPERTY(QVariantList monitors READ monitors NOTIFY monitorsChanged)
public:
    explicit WindowsConnectionRuntime(QObject* parent=nullptr);
    ~WindowsConnectionRuntime() override;
    ConnectionModel* connections(){return &model_;}
    bool active()const;
    bool speakersSupported()const{return true;}
    QString status()const{return status_;}
    QVariantMap devices()const{return devices_;}
    QVariantList monitors()const;
    Q_INVOKABLE void refreshMonitors(){emit monitorsChanged();}
    QVariantMap mirrorStates()const;
    Q_INVOKABLE void connectShared(QString code);
    Q_INVOKABLE void toggle(QString id,bool enabled,QString code={},bool input=true);
    Q_INVOKABLE void disconnectAll();
    Q_INVOKABLE void refreshDevices(QString kind);
    Q_INVOKABLE void setMirror(QString id,bool enabled);
    Q_INVOKABLE void setMuted(QString id,bool enabled);
    Q_INVOKABLE void setVolume(QString id,int percent);
    void shutdown();
signals:
    void changed();
    void devicesChanged();
    void monitorsChanged();
    void usbEvent(QString role,QString event);
    void displayEvent(QString role,QString event);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ConnectionModel model_;
    QVariantMap devices_;
    QString status_="Add connections, choose their sources, then enable them.";
    void poll();
    QString broker(QVariantMap target,QString code);
    void advance(QString brokerKey);
    void launch(QString id,QByteArray credential);
    void stop(QString id,bool forced=false);
    void lost(QString brokerKey,QString error);
    void closeIfIdle(QString brokerKey);
};
