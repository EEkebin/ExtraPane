#pragma once
#include <QThread>
#include <QString>
namespace dm::linux_client {
class Host final:public QThread {
    Q_OBJECT
public:
    explicit Host(QString address,QObject* parent=nullptr):QThread(parent),address_(std::move(address)){}
    static QString unavailable();
signals:
    void state(QString message);
protected:
    void run() override;
private:
    QString address_;
    void serve(int socket);
};
}
