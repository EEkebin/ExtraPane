#pragma once
#include <dm/video_gpu.hpp>
#include <windows.h>

namespace dm::video {
// Native D3D12 draw/present; decoder surfaces are shared on the same adapter.
class D3D12Renderer {
public:
    explicit D3D12Renderer(ID3D11Device* decoder, unsigned width, unsigned height);
    ~D3D12Renderer();
    void present(HWND window, const Frame& frame);
    void detach();
    // Offscreen GPU validation; readback is used only by the diagnostic test.
    Frame diagnostic(const Frame& frame, unsigned width, unsigned height);
    std::string adapter_name() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
void d3d12_renderer_check(unsigned adapter=0);
void renderer_info();
}
