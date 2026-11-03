#pragma once
#include <QObject>
#include <QVariantMap>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QEventLoop>
#include <functional>
#include <set>
#include <dm/input_protocol.hpp>

namespace dm::linux_client {
// Created in the hosting thread. Every portal session owns its virtual monitor
// and input permissions; destruction closes it, including on a network error.
class Portal final:public QObject {
    Q_OBJECT
public:
    explicit Portal(std::function<bool()> cancelled);
    ~Portal() override;
    static QString unavailable();
    void start(bool input);
    void inject(const dm::input::Event& event);
    void release();
    bool closed() const {return closed_;}
    unsigned node=0,width=1920,height=1080,devices=0;
    QDBusUnixFileDescriptor pipewire;
private slots:
    void response(uint code,const QVariantMap& results);
    void sessionClosed();
private:
    QVariantMap request(const QString& interface,const QString& method,QVariantList arguments,QVariantMap options);
    void notify(const QString& method,QVariantList arguments);
    QDBusObjectPath session_;
    std::function<bool()> cancelled_;
    QEventLoop* loop_=nullptr;
    QVariantMap results_;
    unsigned responseCode_=2;
    bool replied_=false,closed_=false;
    std::set<unsigned> keys_,buttons_,touches_;
};
}
