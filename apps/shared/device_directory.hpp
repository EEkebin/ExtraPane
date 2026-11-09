#pragma once
#include <QObject>
#include <QVariantList>
#include <QSet>
#include <QHash>
#include <QNetworkInterface>
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <extrapane/peers.hpp>

// LAN hints only. Never turns a datagram claim into remembered device trust.
class DeviceDirectory final:public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit DeviceDirectory(QObject* parent=nullptr);
    ~DeviceDirectory() override;
    QVariantList devices()const{return devices_;}
    bool scanning()const{return searchTimer_.isActive();}
    bool transportActive()const{return socket_.state()==QAbstractSocket::BoundState;}
    QString status()const{return status_;}
    // Announce only after the actual resource-control listener is ready.
    void advertise(extrapane::peers::Announcement announcement);
    void withdraw();
    void knownDevices(QSet<QString> ids){knownDevices_=std::move(ids);}
    // Transport supports advertisements independently of user-requested browsing.
    bool start(unsigned interfaceIndex=0);
    Q_INVOKABLE bool search(unsigned interfaceIndex=0);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refresh();
signals:
    void changed();
    void knownRoute(QString identity,QString observedAddress,unsigned port);
private:
    friend struct DeviceDirectoryCheck;
    QUdpSocket socket_;
    QTimer timer_,searchTimer_;
    QElapsedTimer clock_;
    QList<QNetworkInterface> interfaces_;
    extrapane::peers::DiscoveryTable table_;
    std::optional<extrapane::peers::Announcement> announcement_;
    QVariantList devices_;
    QSet<QString> knownDevices_;
    QHash<QString,qint64> nextHint_;
    QString status_="Refresh to find nearby devices";
    void beginSearch();
    void finishSearch();
    void observeMessage(const QByteArray&,const QString&,unsigned);
    void receive();
    void publish();
    void send(const extrapane::peers::Bytes&);
    unsigned observedInterface(const QNetworkDatagram&)const;
};
