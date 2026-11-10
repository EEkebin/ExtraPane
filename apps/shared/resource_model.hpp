#pragma once
#include <QAbstractListModel>
#include <QVariantList>
#include <extrapane/capabilities.hpp>

class ResourceModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool canConnect READ canConnect NOTIFY selectionChanged)
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY lockedChanged)
public:
    enum Role {
        Key = Qt::UserRole + 1,
        Name,
        Description,
        Available,
        Reason,
        Selected,
        Devices,
        DeviceSelected,
        Scanning,
        DeviceStatus,
        Running,
        Status,
        Muted
    };
    explicit ResourceModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setCapability(extrapane::ResourceKind kind, extrapane::Availability availability);
    Q_INVOKABLE void select(int row, bool selected);
    bool selected(extrapane::ResourceKind kind) const;
    bool canConnect() const;
    void setDevices(extrapane::ResourceKind kind, QVariantList devices, QString selected, QString status,
                    bool scanning);
    void setSelectedDevice(extrapane::ResourceKind kind, QString id);
    QVariantList devices(extrapane::ResourceKind kind) const;
    QString selectedDevice(extrapane::ResourceKind kind) const;
    bool scanning(extrapane::ResourceKind kind) const;
    void setSessionState(extrapane::ResourceKind kind, bool running, QString status, bool muted = false);
    bool locked() const { return locked_; }
    void setLocked(bool value);
signals:
    void selectionChanged();
    void lockedChanged();

private:
    struct Row {
        extrapane::ResourceKind kind;
        QString key, name, description;
        extrapane::Availability availability;
        bool selected = false;
        QVariantList devices;
        QString deviceSelected, deviceStatus, status;
        bool scanning = false, running = false, muted = false;
    };
    std::vector<Row> rows_;
    bool locked_ = false;
};
