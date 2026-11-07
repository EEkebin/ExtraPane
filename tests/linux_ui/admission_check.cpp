#include "admission.hpp"
#include "linux_admission_fake.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <iostream>
#include <stdexcept>
void require(bool value){if(!value)throw std::runtime_error("Admission lifecycle check failed");}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    const QVariantMap target{{"address","192.0.2.1"},{"localAddress","0.0.0.0"},{"port",43824},{"interfaceIndex",0}};
    LinuxAdmission broker(target,"123456");QStringList received;QString failure;
    QObject::connect(&broker,&LinuxAdmission::ticket,&app,[&](QString id,QString value){require(value.startsWith("EPCT1:"));received.push_back(id);});
    QObject::connect(&broker,&LinuxAdmission::failed,&app,[&](QString error){failure=error;});
    require(broker.request("cancelled",0));broker.cancel("cancelled");require(broker.request("camera",1));require(broker.request("microphone",2));broker.start();
    QElapsedTimer deadline;deadline.start();while(received.size()<2&&deadline.elapsed()<3000){app.processEvents();QThread::msleep(1);}broker.stop();require(broker.wait(1000));require(received==QStringList{"camera","microphone"}&&failure.isEmpty());
    {std::lock_guard lock(dm::linux_client::Tls::mutex);require(dm::linux_client::Tls::kinds==std::vector<unsigned>{1,2});}
    LinuxAdmission bounded(target,"123456");for(unsigned i=0;i<64;++i)require(bounded.request(QString::number(i),0));require(!bounded.request("overflow",0));bounded.cancel("0");require(bounded.request("replacement",1));bounded.stop();require(!bounded.request("stopped",0));
    dm::linux_client::Tls::malformed=true;LinuxAdmission invalid(target,"123456");bool failed=false;QObject::connect(&invalid,&LinuxAdmission::failed,&app,[&](QString){failed=true;});invalid.start();require(invalid.wait(1000));app.processEvents();require(failed);
    std::cout<<"Linux admission cancellation, queue bounds and malformed replies passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
