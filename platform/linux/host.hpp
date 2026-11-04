#pragma once
#include <QThread>
#include <QString>
#include <dm/access.hpp>
namespace dm::linux_client {
class Host final:public QThread {
    Q_OBJECT
public:
    explicit Host(QString address,QObject* parent=nullptr,unsigned port=43824,std::shared_ptr<access::Gate> gate={}):QThread(parent),address_(std::move(address)),port_(port),gate_(std::move(gate)){}
    static QString unavailable();
signals:
    void state(QString message);
protected:
    void run() override;
private:
    QString address_;unsigned port_;std::shared_ptr<access::Gate> gate_;
    void serve(int socket);
};
}
