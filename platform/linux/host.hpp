#pragma once
#include <QThread>
#include <QString>
#include <dm/access.hpp>
#include <atomic>
#include <span>
namespace dm::linux_client {
class Tls;
// This offer invokes the local portal chooser; it is not a pinned physical output.
struct HostScope {int kind=-1;bool existingOnly=false,input=true;};
struct ScreenChooserOffer {unsigned id=0;bool allowInput=false;};
class Host final:public QThread {
    Q_OBJECT
public:
    explicit Host(QString address,QObject* parent=nullptr,unsigned port=43824,std::shared_ptr<access::Gate> gate={},ScreenChooserOffer screen={},HostScope scope={}):QThread(parent),scope_(scope),screen_(screen),address_(std::move(address)),port_(port),gate_(std::move(gate)){}
    ~Host() override;
    void prepareScopedListener(); // Bind only; no capture, portal, or resource activation.
    static QString unavailable(bool existing=false,bool input=true);
signals:
    void state(QString message);
protected:
    void run() override;
private:
    int preparedListener_=-1;
    HostScope scope_;ScreenChooserOffer screen_;
    QString address_;unsigned port_;std::shared_ptr<access::Gate> gate_;
    std::atomic_bool stopping_{false};
    void serve(int socket,std::atomic_uint& pending);
    void serveDisplay(Tls& stream,std::span<const uint8_t> prefix);
};
}
