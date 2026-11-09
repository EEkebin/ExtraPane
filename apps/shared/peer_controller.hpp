#pragma once
#include "device_directory.hpp"
#include <extrapane/service.hpp>
#include <QObject>
#include <QVariantList>
#include <QTimer>
#include <QSet>
#include <memory>

// UI metadata only. Authentication, native bytes and tunnel IO stay in Service.
class PeerController final:public QObject {
    Q_OBJECT
    Q_PROPERTY(DeviceDirectory* directory READ directory CONSTANT)
    Q_PROPERTY(QString deviceName READ deviceName CONSTANT)
    Q_PROPERTY(QString pairingCode READ pairingCode NOTIFY changed)
    Q_PROPERTY(bool pairingRequired READ pairingRequired WRITE setPairingRequired NOTIFY changed)
    Q_PROPERTY(bool gatewayEnabled READ gatewayEnabled WRITE setGatewayEnabled NOTIFY changed)
    Q_PROPERTY(bool sharing READ sharing NOTIFY changed)
    Q_PROPERTY(bool sharingStarting READ sharingStarting NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList networkAddresses READ networkAddresses NOTIFY changed)
    Q_PROPERTY(QVariantList offers READ offers NOTIFY changed)
    Q_PROPERTY(QVariantList remembered READ remembered NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantList connections READ connections NOTIFY changed)
public:
    using Service=extrapane::peers::Service;
    PeerController(Service::Hooks hooks,QString privateDirectory,QObject* parent=nullptr);
    ~PeerController() override;
    DeviceDirectory* directory(){return &directory_;}
    QString deviceName()const{return name_;}
    QString pairingCode()const;
    bool pairingRequired()const{return pairingRequired_;}
    bool gatewayEnabled()const{return gatewayEnabled_;}
    bool sharing()const{return sharing_;}
    bool sharingStarting()const{return sharingStarting_;}
    bool active()const{return (!automaticAvailability_&&(sharing_||sharingStarting_||!devices_.isEmpty()))||!connections_.isEmpty()||!pending_.isEmpty();}
    QString status()const{return status_;}
    QVariantList networkAddresses()const;
    QVariantList offers()const;
    QVariantList remembered()const;
    QVariantList devices()const{return devices_;}
    QVariantList connections()const;
    void setLocalOffers(std::vector<extrapane::peers::Offer>);
    Q_INVOKABLE void refreshOffers();
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void startAvailableSharing(QString address={},unsigned port=43824);
    Q_INVOKABLE void stopIncomingResource(QString id);
    Q_INVOKABLE void disconnectIncomingAll();
    Q_INVOKABLE void enableOffer(QString id,bool enabled);
    Q_INVOKABLE void setOfferSharing(QString id,bool enabled,QString address,unsigned port);
    Q_INVOKABLE void setGatewaySharing(bool enabled,QString address,unsigned port);
    Q_INVOKABLE void setPairingRequired(bool);
    Q_INVOKABLE void setGatewayEnabled(bool);
    Q_INVOKABLE void startSharing(QString address,unsigned port,unsigned interfaceIndex=0);
    Q_INVOKABLE void stopSharing();
    Q_INVOKABLE void connectDevice(QString address,unsigned port,QString code,QString expectedId={},QString localAddress={},bool openMode=false);
    Q_INVOKABLE void disconnectDevice(QString connection);
    Q_INVOKABLE void connectVia(QString gatewayConnection,QString endpoint,QString code={});
    Q_INVOKABLE void forgetDevice(QString identity);
    Q_INVOKABLE void dismissPendingForget(QString identity);
    Q_INVOKABLE QString useResource(QString connection,QString owner,QString resource,bool input=false,unsigned width=0,unsigned height=0,unsigned refresh=0);
    Q_INVOKABLE void stopResource(QString connection,QString grant);
    Q_INVOKABLE void cancelResource(QString connection,QString request);
    Q_INVOKABLE void disconnectAll();
    void shutdown();
signals:
    void changed();
    void refreshRequested();
    void toast(QString message);
private:
    friend struct PeerControllerEventCheck;
    void applyEvent(Service::Event&);
    DeviceDirectory directory_;
    QTimer timer_;
    std::unique_ptr<Service> service_;
    QString name_,status_="Sharing stopped";
    bool automaticAvailability_=false;
    bool pairingRequired_=true,sharing_=false,sharingStarting_=false,gatewayEnabled_=false;
    QString bindAddress_;
    unsigned configuredPort_=0;
    Service::Token sharingGeneration_=0;
    unsigned interfaceIndex_=0,listenPort_=0;
    bool loopbackSharing_=false;
    std::uint64_t announcementInstance_=0,announcementSequence_=0;
    std::vector<extrapane::peers::Offer> inventory_;
    QSet<QString> enabled_;
    QVariantList devices_,connections_;
    QHash<QString,QVariantMap> pending_;
    QHash<QString,QVariantMap> connectionEndpoints_;
    std::vector<extrapane::peers::Offer> selectedOffers()const;
    void poll();
    void announce(unsigned port);
    void error(const QString&);
    void beginSharing(QString address,unsigned port,unsigned interfaceIndex=0);
};
