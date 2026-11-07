#include "admission.hpp"
#ifdef EXTRAPANE_MAC_ADMISSION_TEST
#include "mac_admission_fake.hpp"
#else
#include "transport.hpp"
#endif
#include <dm/control.hpp>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <stdexcept>
namespace dm::mac {
namespace {void wipe(std::string& s){volatile char* p=s.data();for(size_t i=0;i<s.size();++i)p[i]=0;s.clear();}}
class AdmissionBroker {
    struct Request {unsigned kind;std::string credential,error;bool done=false,cancelled=false;~Request(){wipe(credential);}};
    ResourceTarget target_;std::string code_;mutable std::mutex mutex_;std::condition_variable changed_;
    std::deque<std::shared_ptr<Request>> requests_;std::shared_ptr<Transport> stream_;
    std::thread worker_;bool stopping_=false;std::string error_;
    void run(){
        try{
            auto stream=std::make_shared<Transport>(target_.address.c_str(),target_.local.c_str(),target_.port,code_);wipe(code_);
            {std::lock_guard lock(mutex_);if(stopping_)return;stream_=stream;}
            stream->connect();
            auto exchange=[&](access::ControlOperation op,unsigned kind=0){
                stream->write(access::control_request(op,kind));auto bytes=stream->read(128,15);access::ControlReply reply{};
                struct ReplyWipe{std::vector<uint8_t>& bytes;access::ControlReply& reply;~ReplyWipe(){std::fill(bytes.begin(),bytes.end(),0);std::fill(reply.begin(),reply.end(),0);}} guard{bytes,reply};
                std::copy(bytes.begin(),bytes.end(),reply.begin());auto credential=access::control_credential(reply,op,kind,true);
                std::fill(bytes.begin(),bytes.end(),0);std::fill(reply.begin(),reply.end(),0);return credential;
            };
            exchange(access::ControlOperation::open);
            for(;;){
                std::shared_ptr<Request> request;
                {std::unique_lock lock(mutex_);changed_.wait_for(lock,std::chrono::seconds(5),[&]{return stopping_||!requests_.empty();});
                    if(stopping_)break;if(!requests_.empty()){request=requests_.front();requests_.pop_front();if(request->cancelled)continue;}}
                if(!request){exchange(access::ControlOperation::ping);continue;}
                auto credential=exchange(access::ControlOperation::add,request->kind);
                {std::lock_guard lock(mutex_);if(!request->cancelled)request->credential=std::move(credential);request->done=true;changed_.notify_all();}wipe(credential);
            }
        }catch(const std::exception& error){std::lock_guard lock(mutex_);error_=error.what();changed_.notify_all();}
        wipe(code_);
    }
public:
    AdmissionBroker(ResourceTarget target,std::string code):target_(std::move(target)),code_(std::move(code)){worker_=std::thread([this]{run();});}
    ~AdmissionBroker(){std::shared_ptr<Transport> stream;{std::lock_guard lock(mutex_);stopping_=true;stream=stream_;changed_.notify_all();}if(stream)stream->cancel();if(worker_.joinable())worker_.join();wipe(code_);}
    bool alive()const{std::lock_guard lock(mutex_);return !stopping_&&error_.empty();}
    std::string ticket(unsigned kind,const std::function<bool()>& cancelled){
        auto request=std::make_shared<Request>();request->kind=kind;
        std::unique_lock lock(mutex_);if(requests_.size()>=64)throw std::runtime_error("Too many pending resource requests");requests_.push_back(request);changed_.notify_all();
        while(!request->done&&error_.empty()&&!stopping_&&!cancelled())changed_.wait_for(lock,std::chrono::milliseconds(100));
        if(cancelled()){request->cancelled=true;std::erase(requests_,request);throw std::runtime_error("Connection cancelled");}
        if(!error_.empty())throw std::runtime_error(error_);if(stopping_)throw std::runtime_error("Server admission ended");
        return std::move(request->credential);
    }
};
AdmissionLease::~AdmissionLease(){wipe(credential);}
bool AdmissionLease::alive()const{return !broker_||broker_->alive();}
std::shared_ptr<AdmissionLease> acquireAdmission(ResourceTarget target,std::string code,unsigned kind,std::function<bool()> cancelled){
    struct Wipe{std::string& s;~Wipe(){wipe(s);}} guard{code};
    if(kind>3||(!code.empty()&&!access::validCode(code)))throw std::runtime_error("Invalid access code or resource kind");
    static std::mutex mutex;static std::map<std::string,std::weak_ptr<AdmissionBroker>> pool;
    const auto key=target.address+":"+std::to_string(target.port)+"@"+target.local;
    auto lease=std::make_shared<AdmissionLease>();
    {std::lock_guard lock(mutex);for(auto it=pool.begin();it!=pool.end();)if(it->second.expired())it=pool.erase(it);else ++it;
        auto& entry=pool[key];lease->broker_=entry.lock();
        if(!lease->broker_&&!code.empty()){lease->broker_=std::make_shared<AdmissionBroker>(target,code);entry=lease->broker_;}}
    if(cancelled())throw std::runtime_error("Connection cancelled");
    if(lease->broker_)lease->credential=lease->broker_->ticket(kind,cancelled);
    return lease;
}
}
