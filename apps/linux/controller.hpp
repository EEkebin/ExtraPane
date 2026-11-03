#pragma once
#include "input.hpp"
#include "media.hpp"
#include "receiver.hpp"
#include "host.hpp"
#include <QObject>
#include <QQuickWindow>
#include <QVariantList>

class Controller final:public QObject {
    Q_OBJECT
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
    explicit Controller(QString availability,QObject* parent=nullptr);
    ~Controller() override;
    void setWindow(QQuickWindow* window){window_=window;}
    bool hosting()const{return host_!=nullptr;}
    QString hostStatus()const{return hostStatus_;}
    QString hostAvailability()const{return hostAvailability_;}
    Q_INVOKABLE void startHost(QString address);
    Q_INVOKABLE void stopHost();
    bool active()const{return receiver_!=nullptr;}
    QString status()const{return status_;}
    QString availability()const{return availability_;}
    QString savedAddress()const;
    QVariantList addresses()const{return addresses_;}
    QString statistics()const{return statistics_;}
    QString decoder()const{return decoder_;}
    QString version()const;
    unsigned sourceWidth()const{return width_;}
    unsigned sourceHeight()const{return height_;}
    Q_INVOKABLE void refreshAddresses();
    Q_INVOKABLE void attachSurface(QObject* video,QObject* input);
    Q_INVOKABLE void connectTo(QString server,QString local,bool input,unsigned scale,unsigned interfaceIndex);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void releaseInput();
    void shutdown();
signals:
    void changed();
    void hostChanged();
    void addressesChanged();
    void statisticsChanged();
private:
    dm::linux_client::Host* host_=nullptr;
    QString hostStatus_="Server stopped",hostAvailability_;
    dm::linux_client::InputQueue input_;
    dm::linux_client::Media media_;
    dm::linux_client::Receiver* receiver_=nullptr;
    QQuickWindow* window_=nullptr;
    QQuickItem* video_=nullptr;
    dm::linux_client::InputSurface* inputSurface_=nullptr;
    QString availability_,status_="Ready",statistics_,decoder_="Waiting for decoder";
    QVariantList addresses_;
    unsigned width_=1280,height_=720;
    bool failed_=false;
};
