#pragma once
#include <dm/video_gpu.hpp>
#include <windows.h>

namespace dm::video {
class VulkanRenderer {
public:
    explicit VulkanRenderer(ID3D11Device* decoder,unsigned width,unsigned height);
    ~VulkanRenderer();
    bool present(HWND window,const Frame& frame);
    Frame diagnostic(const Frame& frame,unsigned width,unsigned height);
    std::string adapter_name() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
void vulkan_renderer_check(unsigned adapter=0);
}
