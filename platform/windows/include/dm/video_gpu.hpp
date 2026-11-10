#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <dm/video.hpp>

namespace dm::video {
struct GpuFrame {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    unsigned subresource = 0;
};
Microsoft::WRL::ComPtr<ID3D11Texture2D> make_video_texture(ID3D11Device* device, unsigned width, unsigned height,
                                                           DXGI_FORMAT format);
class GpuProcessor {
public:
    explicit GpuProcessor(ID3D11Device* device);
    ~GpuProcessor();
    void blit(const GpuFrame& source, unsigned source_width, unsigned source_height, ID3D11Texture2D* target);
    void wait_idle();
    void submit();

private:
    struct State;
    std::unique_ptr<State> state_;
};
// Diagnostic validation only. Production H264 does not call this readback path.
Frame readback_gpu_bgra(const Frame& frame);
}
