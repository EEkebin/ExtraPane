#pragma once
#include <memory>
#include <functional>
#include <string>
namespace dm::mac {
class Host {
    struct State;std::shared_ptr<State> state_;
public:
    Host(std::string address,std::function<void(std::string)> report);
    ~Host();
    void stop();
    bool running()const;
};
}
