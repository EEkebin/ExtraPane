#pragma once
#include <memory>
#include <functional>
#include <string>
#include <dm/access.hpp>
#include <vector>
#include <cstdint>
namespace dm::mac {
struct SharedScreen {uint32_t id=0;std::string displayUuid;bool allowInput=false;};
// Pin an explicitly selected current display to its persistent OS identity.
SharedScreen shareScreen(uint32_t id,uint32_t display,bool allowInput=false);
class Host {
    struct State;std::shared_ptr<State> state_;
public:
    Host(std::string address,std::function<void(std::string)> report,unsigned port=43824,std::shared_ptr<access::Gate> gate={},std::vector<SharedScreen> screens={});
    ~Host();
    void stop();
    bool running()const;
};
}
