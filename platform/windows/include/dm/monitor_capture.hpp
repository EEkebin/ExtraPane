#pragma once
#include <dm/video.hpp>
#include <string>
struct ID3D11Device;
namespace dm::video {
// WGC can target an indirect monitor without assuming it is a DXGI output
// belonging to the hardware encoder's adapter.
class MonitorCapture {
public:
    MonitorCapture(ID3D11Device* device,const std::wstring& source,unsigned width,unsigned height);
    ~MonitorCapture();
    Frame next_gpu();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
