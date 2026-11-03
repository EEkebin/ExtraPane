#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <dm/input_protocol.hpp>
#include <set>
#include <chrono>
namespace dm::mac {
class HostInput {
    unsigned slot_;CGDirectDisplayID display_;std::set<unsigned> keys_,buttons_;CGEventSourceRef source_=nullptr;CGPoint last_{},clickPoint_{};unsigned click_=0,lastButton_=0;std::chrono::steady_clock::time_point lastClick_{};
public:
    HostInput(unsigned slot,CGDirectDisplayID display);
    ~HostInput();
    void reset();
    void inject(const input::Event& event);
};
}
