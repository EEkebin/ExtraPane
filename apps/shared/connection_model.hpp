#pragma once
#include <QAbstractListModel>
#include <QVariantMap>
#include <vector>

// Configuration and lifecycle metadata only; media stays in native engines.
class ConnectionModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool singleServer READ singleServer WRITE setSingleServer NOTIFY destinationModeChanged)
    Q_PROPERTY(QVariantMap sharedTarget READ sharedTarget NOTIFY sharedTargetChanged)
    Q_PROPERTY(int activeConnectionCount READ activeConnectionCount NOTIFY activityChanged)
    Q_PROPERTY(bool hasReservedResources READ hasReservedResources NOTIFY activityChanged)
public:
    enum Role {
        Id = Qt::UserRole + 1,
        Kind,
        Enabled,
        Source,
        Target,
        EffectiveTarget,
        DisplayMode,
        Phase,
        Status,
        Busy,
        DestinationVisible
    };
    enum class State { idle, connecting, active, returning, recovery, failed };
    explicit ConnectionModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool singleServer() const { return singleServer_; }
    void setSingleServer(bool value);
    QVariantMap sharedTarget() const { return sharedTarget_; }
    int activeConnectionCount() const;
    bool hasReservedResources() const;
    Q_INVOKABLE bool setSharedTarget(QVariantMap target);
    Q_INVOKABLE QString addConnection(QString kind);
    Q_INVOKABLE bool removeConnection(QString id);
    Q_INVOKABLE bool setEnabled(QString id, bool value);
    Q_INVOKABLE bool setSource(QString id, QString source);
    Q_INVOKABLE bool setTarget(QString id, QVariantMap target);
    Q_INVOKABLE bool setDisplayMode(QString id, QVariantMap mode);
    Q_INVOKABLE QVariantMap connection(QString id) const;
    // Controller calls before acquiring native ownership. A false result leaves
    // lifecycle unchanged and supplies a row-specific validation error.
    bool beginConnection(QString id);
    bool setState(QString id, State state, QString status = {});
signals:
    void destinationModeChanged();
    void sharedTargetChanged();
    void configurationChanged(QString id);
    void enabledChanged(QString id, bool enabled);
    void activityChanged();

private:
    struct Row {
        QString id, kind, source, status;
        bool enabled = false;
        QVariantMap target, heldTarget, display;
        State state = State::idle;
    };
    static bool busy(State state);
    static QString phase(State state);
    static bool validTarget(const QVariantMap& target, bool complete);
    static bool validDisplay(const QVariantMap& mode);
    int find(const QString& id) const;
    QVariantMap effective(const Row& row) const;
    void changed(int row);
    std::vector<Row> rows_;
    bool singleServer_ = true;
    QVariantMap sharedTarget_{{"address", ""}, {"port", 43824}, {"localAddress", "0.0.0.0"}, {"interfaceIndex", 0}};
};
