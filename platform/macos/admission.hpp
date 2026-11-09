#pragma once
#include <functional>
#include <memory>
#include <string>
namespace dm::mac {
struct ResourceTarget {std::string address,local;unsigned port=43824;};
class AdmissionBroker;
class AdmissionLease {
    friend std::shared_ptr<AdmissionLease> acquireAdmission(ResourceTarget,std::string,unsigned,std::function<bool()>);
    std::shared_ptr<AdmissionBroker> broker_;
public:
    std::string credential;
    ~AdmissionLease();
    bool alive()const;
};
std::shared_ptr<AdmissionLease> acquireAdmission(ResourceTarget target,std::string code,unsigned kind,std::function<bool()> cancelled);
}
