#include "admission.hpp"
#ifdef EXTRAPANE_ADMISSION_TEST
#include "linux_admission_fake.hpp"
#else
#include "tls.hpp"
#endif
#include <dm/control.hpp>
#include <chrono>

LinuxAdmission::LinuxAdmission(QVariantMap target,QString code,QObject* parent)
    :QThread(parent),target_(std::move(target)),code_(std::move(code)){}
bool LinuxAdmission::request(QString id,unsigned kind){
    {std::lock_guard lock(mutex_);if(pending_.size()>=64||isInterruptionRequested())return false;pending_.emplace_back(std::move(id),kind);}changed_.notify_all();return true;
}
void LinuxAdmission::cancel(QString id){std::lock_guard lock(mutex_);std::erase_if(pending_,[&](const auto& item){return item.first==id;});}
void LinuxAdmission::stop(){requestInterruption();changed_.notify_all();}
void LinuxAdmission::run(){
    using namespace dm::access;using Clock=std::chrono::steady_clock;
    try{
        auto secret=code_.toStdString();
        struct Wipe{std::string& value;~Wipe(){volatile char* bytes=value.data();for(size_t i=0;i<value.size();++i)bytes[i]=0;}} wipe{secret};
        dm::linux_client::Tls stream(target_["address"].toString().toStdString(),target_["localAddress"].toString().toStdString(),
            static_cast<uint16_t>(target_["port"].toUInt()),[this]{return isInterruptionRequested();},target_["interfaceIndex"].toUInt(),secret);
        std::fill(secret.begin(),secret.end(),0);secret.clear();
        code_.fill(QChar(0));code_.clear();
        auto exchange=[&](ControlOperation op,unsigned kind=0){
            stream.write(control_request(op,kind),Clock::now()+std::chrono::seconds(15));
            ControlReply reply{};
            struct WipeReply{ControlReply& value;~WipeReply(){volatile uint8_t* bytes=value.data();for(size_t i=0;i<value.size();++i)bytes[i]=0;}} wipeReply{reply};
            stream.read(reply,Clock::now()+std::chrono::seconds(15));
            return control_credential(reply,op,kind,true);
        };
        exchange(ControlOperation::open);
        while(!isInterruptionRequested()){
            std::pair<QString,unsigned> next;
            {std::unique_lock lock(mutex_);changed_.wait_for(lock,std::chrono::seconds(5),[this]{return isInterruptionRequested()||!pending_.empty();});
                if(isInterruptionRequested())break;if(!pending_.empty()){next=std::move(pending_.front());pending_.pop_front();}}
            if(next.first.isEmpty()){exchange(ControlOperation::ping);continue;}
            auto credential=exchange(ControlOperation::add,next.second);
            emit ticket(next.first,QString::fromStdString(credential));std::fill(credential.begin(),credential.end(),0);
        }
        // Closing TLS invalidates the owner's pending tickets; interruption keeps shutdown bounded.
    }catch(const std::exception& error){if(!isInterruptionRequested())emit failed(QString::fromUtf8(error.what()));}
    code_.fill(QChar(0));code_.clear();
}
