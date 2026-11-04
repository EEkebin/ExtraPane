#pragma once
#include <memory>
#include <functional>
#include <string>
#include <dm/access.hpp>
namespace dm::mac {
class Host {
    struct State;std::shared_ptr<State> state_;
public:
    Host(std::string address,std::function<void(std::string)> report,unsigned port=43824,std::shared_ptr<access::Gate> gate={});
    ~Host();
    void stop();
    bool running()const;
};
}
