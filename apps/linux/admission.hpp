#pragma once
#include <QThread>
#include <QVariantMap>
#include <condition_variable>
#include <mutex>
#include <deque>

// One authenticated control channel per destination; no media passes through it.
class LinuxAdmission final : public QThread {
    Q_OBJECT
public:
    LinuxAdmission(QVariantMap target, QString code, QObject* parent = nullptr);
    bool request(QString id, unsigned kind);
    void cancel(QString id);
    void stop();
signals:
    void ticket(QString id, QString credential);
    void failed(QString reason);

protected:
    void run() override;

private:
    QVariantMap target_;
    QString code_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::pair<QString, unsigned>> pending_;
};
