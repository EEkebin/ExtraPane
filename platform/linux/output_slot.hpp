#pragma once
#include <memory>
#include <stdexcept>
#include <string>

namespace dm::linux_client {
// Acquire must reject foreign/busy nodes before configuration and return an
// owning handle. Destruction releases the descriptor/lease on every failed try.
template<class Handle,class Range,class Acquire,class Configure>
std::unique_ptr<Handle> reserveOutputSlot(const Range& candidates,Acquire acquire,Configure configure){
    std::string lastFailure;
    for(const auto& candidate:candidates){
        try{
            auto handle=acquire(candidate);
            if(!handle)continue;
            configure(*handle);
            return handle;
        }catch(const std::runtime_error& error){lastFailure=error.what();}
    }
    if(!lastFailure.empty())throw std::runtime_error("No compatible ExtraPane Camera slot: "+lastFailure);
    throw std::runtime_error("No writable, unused ExtraPane Camera v4l2loopback slot is available");
}
}
