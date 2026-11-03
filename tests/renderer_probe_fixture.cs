// UI-only unavailable-device fixture. Never shipped in the app package.
using System;
public static class RendererProbeFixture {
    public static void Main() {
        Console.WriteLine("RENDERER_INFO d3d11 available Test adapter");
        Console.WriteLine("RENDERER_INFO d3d12 unavailable The graphics driver cannot share decoder textures with D3D12.");
        Console.WriteLine("RENDERER_INFO vulkan unavailable Vulkan runtime is missing. Install or update the graphics driver.");
    }
}
