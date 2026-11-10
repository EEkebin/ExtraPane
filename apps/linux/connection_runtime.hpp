#pragma once
#include "connection_model.hpp"
#include <QObject>
#include <QVariantList>
#include <QPointer>
#include <QSet>
#include <memory>
#include <map>
class QQmlEngine;
class LinuxAdmission;

class LinuxConnectionRuntime final : public QObject {
    Q_OBJECT
    Q_PROPERTY(ConnectionModel* connections READ connections CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantMap mirrorStates READ mirrorStates NOTIFY changed)
    Q_PROPERTY(QVariantList monitors READ monitors NOTIFY monitorsChanged)
public:
    explicit LinuxConnectionRuntime(QString availability, QObject* parent = nullptr);
    ~LinuxConnectionRuntime() override;
    ConnectionModel* connections() { return &model_; }
    void setEngine(QQmlEngine* engine) { engine_ = engine; }
    bool active() const;
    QString status() const { return status_; }
    QVariantMap devices() const { return devices_; }
    QVariantMap mirrorStates() const { return mirrors_; }
    QVariantList monitors() const;
    Q_INVOKABLE void refreshMonitors() { emit monitorsChanged(); }
    Q_INVOKABLE void refreshDevices(QString kind);
    Q_INVOKABLE void toggle(QString id, bool enabled, QString code = {}, bool input = true);
    Q_INVOKABLE void disconnectAll();
    Q_INVOKABLE void setMirror(QString id, bool enabled);
    Q_INVOKABLE void setMuted(QString id, bool enabled);
    void releaseInput();
    void shutdown();
signals:
    void changed();
    void devicesChanged();
    void monitorsChanged();

private:
    struct Row;
    std::map<QString, std::unique_ptr<Row>> rows_;
    std::map<QString, QPointer<LinuxAdmission>> admissions_;
    ConnectionModel model_;
    QVariantMap devices_, mirrors_;
    QSet<QString> scanning_;
    QString availability_, status_ = "Add resources, choose their sources, then enable them.";
    QQmlEngine* engine_ = nullptr;
    bool shuttingDown_ = false;
    uint64_t nextGeneration_ = 0;
    void launch(QString id, QString credential);
    void finish(QString id, QString error = {});
    void closeAdmission(QString key);
};
