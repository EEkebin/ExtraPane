#pragma once
#include <QObject>
#include <QVariantList>
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
    bool scanning()const{return socket_.state()==QAbstractSocket::BoundState;}
    QString status()const{return status_;}
    // Announce only after the actual resource-control listener is ready.
    void advertise(extrapane::peers::Announcement announcement);
    void withdraw();
    Q_INVOKABLE bool start(unsigned interfaceIndex=0);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refresh();
signals:
    void changed();
private:
    QUdpSocket socket_;
    QTimer timer_;
    QElapsedTimer clock_;
    QList<QNetworkInterface> interfaces_;
    extrapane::peers::DiscoveryTable table_;
    std::optional<extrapane::peers::Announcement> announcement_;
    QVariantList devices_;
    QString status_="Discovery stopped";
    void receive();
    void publish();
    void send(const extrapane::peers::Bytes&);
    unsigned observedInterface(const QNetworkDatagram&)const;
};
