#pragma once
#include <windows.h>
#include <dm/input_protocol.hpp>
#include <memory>
#include <string>
namespace dm::input {
unsigned receiver_capabilities();
class ReceiverInput {
public:
    explicit ReceiverInput(bool enabled);
    ~ReceiverInput();
    void grant(unsigned caps);
    void dimensions(unsigned width, unsigned height);
    bool message(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void pump();
    std::vector<Event> drain();

private:
    struct State;
    std::unique_ptr<State> state_;
};
class Broker {
public:
    explicit Broker(const std::wstring& source);
    ~Broker();
    unsigned capabilities() const;
    void send(std::span<const Event> events);

private:
    struct State;
    std::unique_ptr<State> state_;
};
int broker_main(const std::wstring& source);
}
