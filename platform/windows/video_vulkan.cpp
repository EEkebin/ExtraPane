#include <dm/video_vulkan.hpp>
#include <dm/video_layout.hpp>
#include <d3d10_1.h>
#include <dxgi1_2.h>
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace dm::video {
using Microsoft::WRL::ComPtr;
namespace {
void check(VkResult result,const char* stage){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(stage)+" (Vulkan result "+std::to_string(result)+")");}
void dxcheck(HRESULT result,const char* stage){if(FAILED(result))throw std::runtime_error(std::string(stage)+" (HRESULT "+std::to_string(static_cast<unsigned long>(result))+")");}
struct Handle{HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}};
struct Library{HMODULE value=nullptr;~Library(){if(value)FreeLibrary(value);}};
struct ContextLock{ID3D10Multithread* value;explicit ContextLock(ID3D10Multithread* p):value(p){value->Enter();}~ContextLock(){value->Leave();}};
constexpr auto external_type=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
constexpr auto format=VK_FORMAT_B8G8R8A8_UNORM;
constexpr std::uint64_t timeout_ns=5'000'000'000ull;
#define INSTANCE_FUNCTIONS(X) \
 X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties2) X(GetPhysicalDeviceQueueFamilyProperties) \
 X(EnumerateDeviceExtensionProperties) X(GetPhysicalDeviceImageFormatProperties2) X(GetPhysicalDeviceFormatProperties) \
 X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceSurfaceSupportKHR) X(GetPhysicalDeviceSurfaceCapabilitiesKHR) \
 X(GetPhysicalDeviceSurfaceFormatsKHR) X(CreateWin32SurfaceKHR) X(DestroySurfaceKHR) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) \
 X(DestroyDevice) X(GetDeviceQueue) X(DeviceWaitIdle) X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
 X(ResetCommandPool) X(BeginCommandBuffer) X(EndCommandBuffer) X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) \
 X(CreateSemaphore) X(DestroySemaphore) X(QueueSubmit) X(QueuePresentKHR) X(CreateSwapchainKHR) X(DestroySwapchainKHR) \
 X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) \
 X(AllocateMemory) X(FreeMemory) X(BindImageMemory) X(GetMemoryWin32HandlePropertiesKHR) X(CmdPipelineBarrier) \
 X(CmdClearColorImage) X(CmdBlitImage) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) \
 X(CmdCopyImageToBuffer) X(MapMemory) X(UnmapMemory)
}
struct VulkanRenderer::State {
    Library library;PFN_vkGetInstanceProcAddr get_instance=nullptr;
#define DECLARE(name) PFN_vk##name vk##name=nullptr;
    INSTANCE_FUNCTIONS(DECLARE)
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
    VkInstance instance=VK_NULL_HANDLE;VkPhysicalDevice physical=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;VkQueue queue=VK_NULL_HANDLE;
    VkSurfaceKHR surface=VK_NULL_HANDLE;VkSwapchainKHR swap=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer commands=VK_NULL_HANDLE;VkFence finished=VK_NULL_HANDLE;VkSemaphore acquired=VK_NULL_HANDLE;
    VkImage shared_image=VK_NULL_HANDLE;VkDeviceMemory shared_memory=VK_NULL_HANDLE;
    std::vector<VkImage> images;std::vector<VkSemaphore> rendered;
    ComPtr<ID3D11Device> decoder;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D10Multithread> threading;
    ComPtr<ID3D11Texture2D> shared_texture;ComPtr<IDXGIKeyedMutex> keyed;
    std::unique_ptr<GpuProcessor> processor;
    HWND probe_window=nullptr,attached_window=nullptr;
    UINT family=0,texture_width=0,texture_height=0,window_width=0,window_height=0;
    VkExtent2D extent{};bool submitted=false,rebuild=false;std::string name;

    ~State(){
        if(device&&vkDeviceWaitIdle)vkDeviceWaitIdle(device);
        clear_swap();clear_texture();
        if(device){if(acquired&&vkDestroySemaphore)vkDestroySemaphore(device,acquired,nullptr);if(finished&&vkDestroyFence)vkDestroyFence(device,finished,nullptr);if(pool&&vkDestroyCommandPool)vkDestroyCommandPool(device,pool,nullptr);if(vkDestroyDevice)vkDestroyDevice(device,nullptr);}
        if(surface&&vkDestroySurfaceKHR)vkDestroySurfaceKHR(instance,surface,nullptr);
        if(instance&&vkDestroyInstance)vkDestroyInstance(instance,nullptr);
        if(probe_window)DestroyWindow(probe_window);
    }
    void initialize(ID3D11Device* source,unsigned width,unsigned height){
        decoder=source;decoder->GetImmediateContext(&context);dxcheck(context.As(&threading),"Decoder context synchronization unavailable");
        processor=std::make_unique<GpuProcessor>(source);
        library.value=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!library.value)throw std::runtime_error("Vulkan runtime is missing. Install or update the GPU manufacturer's graphics driver.");
        get_instance=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library.value,"vkGetInstanceProcAddr"));
        if(!get_instance)throw std::runtime_error("Vulkan loader does not expose vkGetInstanceProcAddr");
        auto create=reinterpret_cast<PFN_vkCreateInstance>(get_instance(nullptr,"vkCreateInstance"));
        auto enumerate=reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(get_instance(nullptr,"vkEnumerateInstanceExtensionProperties"));
        if(!create||!enumerate)throw std::runtime_error("Vulkan loader is incomplete");
        UINT count=0;check(enumerate(nullptr,&count,nullptr),"Enumerate Vulkan instance extensions");std::vector<VkExtensionProperties> extensions(count);check(enumerate(nullptr,&count,extensions.data()),"Read Vulkan instance extensions");
        const char* instance_extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        for(auto required:instance_extensions)if(std::none_of(extensions.begin(),extensions.end(),[&](const auto& e){return std::strcmp(e.extensionName,required)==0;}))throw std::runtime_error(std::string("Vulkan driver is missing ")+required);
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="SideScreen";app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;info.enabledExtensionCount=2;info.ppEnabledExtensionNames=instance_extensions;
        check(create(&info,nullptr,&instance),"Vulkan 1.1 initialization unavailable; update the graphics driver");
#define LOAD_INSTANCE(name) vk##name=reinterpret_cast<PFN_vk##name>(get_instance(instance,"vk" #name));if(!vk##name)throw std::runtime_error("Vulkan driver is missing vk" #name);
        INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
        ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC adapter_desc{};
        dxcheck(decoder.As(&dxgi),"Find decoder DXGI device");dxcheck(dxgi->GetAdapter(&adapter),"Find decoder GPU");dxcheck(adapter->GetDesc(&adapter_desc),"Find decoder GPU identity");
        check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"Enumerate Vulkan GPUs");std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"Read Vulkan GPUs");
        for(auto candidate:devices){VkPhysicalDeviceIDProperties identity{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&identity;vkGetPhysicalDeviceProperties2(candidate,&properties);if(identity.deviceLUIDValid&&std::memcmp(identity.deviceLUID,&adapter_desc.AdapterLuid,VK_LUID_SIZE)==0){physical=candidate;name=properties.properties.deviceName;if(properties.properties.apiVersion<VK_API_VERSION_1_1)throw std::runtime_error("The decoder GPU requires a Vulkan 1.1 graphics driver");break;}}
        if(!physical)throw std::runtime_error("No Vulkan GPU matches the active D3D11 decoder GPU. Update that adapter's graphics driver.");
        check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr),"Enumerate Vulkan GPU extensions");extensions.resize(count);check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,extensions.data()),"Read Vulkan GPU extensions");
        const char* device_extensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME,VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME};
        for(auto required:device_extensions)if(std::none_of(extensions.begin(),extensions.end(),[&](const auto& e){return std::strcmp(e.extensionName,required)==0;}))throw std::runtime_error(std::string("Decoder GPU is missing ")+required);
        probe_window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"SideScreen Vulkan support check",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!probe_window)throw std::runtime_error("Cannot create a hidden Vulkan presentation probe");make_surface(probe_window);
        vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());bool found=false;
        for(UINT i=0;i<count;++i){VkBool32 present=VK_FALSE;check(vkGetPhysicalDeviceSurfaceSupportKHR(physical,i,surface,&present),"Check Vulkan Windows presentation");if((families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)&&present){family=i;found=true;break;}}
        if(!found)throw std::runtime_error("Decoder GPU cannot present Vulkan windows on this PC");
        VkFormatProperties format_properties{};vkGetPhysicalDeviceFormatProperties(physical,format,&format_properties);
        const auto required=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if((format_properties.optimalTilingFeatures&required)!=required)throw std::runtime_error("Vulkan driver lacks BGRA linear scaling support");
        VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};external.handleType=external_type;
        VkPhysicalDeviceImageFormatInfo2 image_info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};image_info.pNext=&external;image_info.format=format;image_info.type=VK_IMAGE_TYPE_2D;image_info.tiling=VK_IMAGE_TILING_OPTIMAL;image_info.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VkExternalImageFormatProperties external_properties{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};VkImageFormatProperties2 image_properties{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};image_properties.pNext=&external_properties;
        check(vkGetPhysicalDeviceImageFormatProperties2(physical,&image_info,&image_properties),"Vulkan cannot import D3D11 BGRA textures");
        if(!(external_properties.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))throw std::runtime_error("Vulkan driver cannot import decoder GPU textures");
        float priority=1;VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;device_info.enabledExtensionCount=3;device_info.ppEnabledExtensionNames=device_extensions;
        check(vkCreateDevice(physical,&device_info,nullptr,&device),"Create Vulkan renderer device");
#define LOAD_DEVICE(name) vk##name=reinterpret_cast<PFN_vk##name>(vkGetDeviceProcAddr(device,"vk" #name));if(!vk##name)throw std::runtime_error("Vulkan driver is missing vk" #name);
        DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
        vkGetDeviceQueue(device,family,0,&queue);
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool_info.queueFamilyIndex=family;pool_info.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;check(vkCreateCommandPool(device,&pool_info,nullptr,&pool),"Create Vulkan command pool");
        VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};command_info.commandPool=pool;command_info.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_info.commandBufferCount=1;check(vkAllocateCommandBuffers(device,&command_info,&commands),"Create Vulkan commands");
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device,&fence_info,nullptr,&finished),"Create Vulkan completion fence");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};check(vkCreateSemaphore(device,&semaphore,nullptr,&acquired),"Create Vulkan acquire semaphore");
        texture(width,height);create_swap(probe_window); // Probe actual WSI format/usage as well as imports.
        if(keyed->AcquireSync(0,5000)!=S_OK)throw std::runtime_error("Vulkan texture ownership probe timed out or was abandoned");dxcheck(keyed->ReleaseSync(1),"Release Vulkan probe texture");
        begin();submit(VK_NULL_HANDLE,VK_NULL_HANDLE);wait();clear_swap();
    }
    void make_surface(HWND window){VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};info.hinstance=GetModuleHandleW(nullptr);info.hwnd=window;check(vkCreateWin32SurfaceKHR(instance,&info,nullptr,&surface),"Create Vulkan Windows surface");attached_window=window;}
    UINT memory_type(UINT bits,VkMemoryPropertyFlags flags){VkPhysicalDeviceMemoryProperties properties{};vkGetPhysicalDeviceMemoryProperties(physical,&properties);for(UINT i=0;i<properties.memoryTypeCount;++i)if((bits&(1u<<i))&&(properties.memoryTypes[i].propertyFlags&flags)==flags)return i;throw std::runtime_error("Vulkan has no compatible GPU memory type");}
    void wait(){if(submitted){check(vkWaitForFences(device,1,&finished,VK_TRUE,timeout_ns),"Vulkan GPU completion timed out or failed");submitted=false;}}
    void clear_swap(){if(!device)return;for(auto semaphore:rendered)if(semaphore&&vkDestroySemaphore)vkDestroySemaphore(device,semaphore,nullptr);rendered.clear();images.clear();if(swap&&vkDestroySwapchainKHR)vkDestroySwapchainKHR(device,swap,nullptr);swap=VK_NULL_HANDLE;window_width=window_height=0;}
    void clear_texture(){if(device){if(shared_image&&vkDestroyImage)vkDestroyImage(device,shared_image,nullptr);if(shared_memory&&vkFreeMemory)vkFreeMemory(device,shared_memory,nullptr);}shared_image=VK_NULL_HANDLE;shared_memory=VK_NULL_HANDLE;keyed.Reset();shared_texture.Reset();texture_width=texture_height=0;}
    void texture(unsigned width,unsigned height){
        if(shared_image&&texture_width==width&&texture_height==height)return;wait();clear_texture();
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        dxcheck(decoder->CreateTexture2D(&desc,nullptr,&shared_texture),"Decoder GPU cannot create a Vulkan-shared texture");dxcheck(shared_texture.As(&keyed),"Decoder GPU cannot share a keyed mutex");
        ComPtr<IDXGIResource1> resource;dxcheck(shared_texture.As(&resource),"Shared GPU texture interface");Handle handle;dxcheck(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle.value),"Create Vulkan texture share handle");
        VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};external.handleTypes=external_type;
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.pNext=&external;image.imageType=VK_IMAGE_TYPE_2D;image.format=format;image.extent={width,height,1};image.mipLevels=1;image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;image.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device,&image,nullptr,&shared_image),"Create Vulkan shared image");VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device,shared_image,&requirements);
        VkMemoryWin32HandlePropertiesKHR properties{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};check(vkGetMemoryWin32HandlePropertiesKHR(device,external_type,handle.value,&properties),"Inspect D3D11 texture import memory");
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.image=shared_image;
        VkImportMemoryWin32HandleInfoKHR imported{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};imported.pNext=&dedicated;imported.handleType=external_type;imported.handle=handle.value;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.pNext=&imported;allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=memory_type(requirements.memoryTypeBits&properties.memoryTypeBits,0);
        check(vkAllocateMemory(device,&allocation,nullptr,&shared_memory),"Import D3D11 texture into Vulkan");check(vkBindImageMemory(device,shared_image,shared_memory,0),"Bind Vulkan shared image");texture_width=width;texture_height=height;
    }
    void convert(const Frame& frame){
        if(!frame.gpu||frame.gpu->device.Get()!=decoder.Get()||!frame.bgra.empty())throw std::runtime_error("Vulkan requires GPU-decoded frames on the same adapter");
        texture(frame.width,frame.height);
        const auto acquired_key=keyed->AcquireSync(0,5000);if(acquired_key!=S_OK)throw std::runtime_error("Vulkan texture ownership timed out or was abandoned");
        try{processor->blit(*frame.gpu,frame.width,frame.height,shared_texture.Get());processor->submit();dxcheck(keyed->ReleaseSync(1),"Release decoded frame to Vulkan");}
        catch(...){keyed->ReleaseSync(0);throw;}
    }
    void begin(){wait();check(vkResetCommandPool(device,pool,0),"Reset Vulkan commands");VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin_info.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(commands,&begin_info),"Begin Vulkan frame");}
    void barrier(VkImage image,VkImageLayout before,VkImageLayout after,VkAccessFlags source,VkAccessFlags destination,UINT from=VK_QUEUE_FAMILY_IGNORED,UINT to=VK_QUEUE_FAMILY_IGNORED){VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.srcAccessMask=source;barrier.dstAccessMask=destination;barrier.oldLayout=before;barrier.newLayout=after;barrier.srcQueueFamilyIndex=from;barrier.dstQueueFamilyIndex=to;barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);}
    void draw(VkImage output,unsigned width,unsigned height,VkImageLayout after){
        // Imported D3D11 images are GENERAL outside Vulkan. Ownership and
        // contents arrive through the keyed mutex, never UNDEFINED transitions.
        barrier(shared_image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,0,VK_ACCESS_TRANSFER_READ_BIT,VK_QUEUE_FAMILY_EXTERNAL,family);
        barrier(output,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkClearColorValue black{};black.float32[3]=1;VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdClearColorImage(commands,output,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&black,1,&range);
        barrier(output,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        const auto rect=content_rect(texture_width,texture_height,width,height);VkImageBlit blit{};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.srcOffsets[1]={static_cast<int>(texture_width),static_cast<int>(texture_height),1};blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.dstOffsets[0]={static_cast<int>(rect.x),static_cast<int>(rect.y),0};blit.dstOffsets[1]={static_cast<int>(rect.x+rect.width),static_cast<int>(rect.y+rect.height),1};vkCmdBlitImage(commands,shared_image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_LINEAR);
        barrier(output,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,after,VK_ACCESS_TRANSFER_WRITE_BIT,after==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL?VK_ACCESS_TRANSFER_READ_BIT:0);
        barrier(shared_image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,0,family,VK_QUEUE_FAMILY_EXTERNAL);
    }
    void submit(VkSemaphore available,VkSemaphore ready){
        check(vkEndCommandBuffer(commands),"End Vulkan frame");check(vkResetFences(device,1,&finished),"Reset Vulkan frame fence");
        const UINT64 acquire_key=1,release_key=0;const UINT timeout=5000;
        VkWin32KeyedMutexAcquireReleaseInfoKHR keyed_info{VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR};keyed_info.acquireCount=1;keyed_info.pAcquireSyncs=&shared_memory;keyed_info.pAcquireKeys=&acquire_key;keyed_info.pAcquireTimeouts=&timeout;keyed_info.releaseCount=1;keyed_info.pReleaseSyncs=&shared_memory;keyed_info.pReleaseKeys=&release_key;
        VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit_info.pNext=&keyed_info;submit_info.commandBufferCount=1;submit_info.pCommandBuffers=&commands;
        if(available){submit_info.waitSemaphoreCount=1;submit_info.pWaitSemaphores=&available;submit_info.pWaitDstStageMask=&stage;}if(ready){submit_info.signalSemaphoreCount=1;submit_info.pSignalSemaphores=&ready;}
        check(vkQueueSubmit(queue,1,&submit_info,finished),"Submit Vulkan GPU frame");submitted=true;
    }
    void create_swap(HWND window){
        if(swap||attached_window!=window){check(vkDeviceWaitIdle(device),"Wait before resizing Vulkan presentation");submitted=false;clear_swap();}
        if(attached_window!=window){vkDestroySurfaceKHR(instance,surface,nullptr);surface=VK_NULL_HANDLE;make_surface(window);VkBool32 supported=VK_FALSE;check(vkGetPhysicalDeviceSurfaceSupportKHR(physical,family,surface,&supported),"Check Vulkan window presentation");if(!supported)throw std::runtime_error("Vulkan GPU cannot present this window");}
        VkSurfaceCapabilitiesKHR capabilities{};check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&capabilities),"Read Vulkan window capabilities");
        if(!(capabilities.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT))throw std::runtime_error("Vulkan window does not support GPU image transfers");
        UINT count=0;check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,nullptr),"Enumerate Vulkan window formats");std::vector<VkSurfaceFormatKHR> formats(count);check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,formats.data()),"Read Vulkan window formats");
        if(std::none_of(formats.begin(),formats.end(),[](auto f){return (f.format==format||f.format==VK_FORMAT_UNDEFINED)&&f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;}))throw std::runtime_error("Vulkan window lacks a compatible SDR BGRA format");
        RECT client{};GetClientRect(window,&client);window_width=static_cast<UINT>(std::max(1L,client.right));window_height=static_cast<UINT>(std::max(1L,client.bottom));extent=capabilities.currentExtent;
        if(extent.width==std::numeric_limits<UINT>::max())extent={std::clamp(window_width,capabilities.minImageExtent.width,capabilities.maxImageExtent.width),std::clamp(window_height,capabilities.minImageExtent.height,capabilities.maxImageExtent.height)};
        if(!extent.width||!extent.height)throw std::runtime_error("Vulkan window has no drawable area");
        auto image_count=std::max(2u,capabilities.minImageCount);if(capabilities.maxImageCount)image_count=std::min(image_count,capabilities.maxImageCount);
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};info.surface=surface;info.minImageCount=image_count;info.imageFormat=format;info.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;info.imageExtent=extent;info.imageArrayLayers=1;info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;info.preTransform=capabilities.currentTransform;info.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;info.presentMode=VK_PRESENT_MODE_FIFO_KHR;info.clipped=VK_TRUE;
        if(!(capabilities.supportedCompositeAlpha&info.compositeAlpha))throw std::runtime_error("Vulkan window lacks opaque presentation support");
        check(vkCreateSwapchainKHR(device,&info,nullptr,&swap),"Create Vulkan swapchain");check(vkGetSwapchainImagesKHR(device,swap,&count,nullptr),"Count Vulkan backbuffers");images.resize(count);check(vkGetSwapchainImagesKHR(device,swap,&count,images.data()),"Read Vulkan backbuffers");rendered.resize(count,VK_NULL_HANDLE);VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};for(auto& ready:rendered)check(vkCreateSemaphore(device,&semaphore,nullptr,&ready),"Create Vulkan present semaphore");rebuild=false;
    }
};
VulkanRenderer::VulkanRenderer(ID3D11Device* decoder,unsigned width,unsigned height):state_(std::make_unique<State>()){state_->initialize(decoder,width,height);}
VulkanRenderer::~VulkanRenderer()=default;
std::string VulkanRenderer::adapter_name()const{return state_->name;}
bool VulkanRenderer::present(HWND window,const Frame& frame){
    auto& s=*state_;RECT client{};GetClientRect(window,&client);if(client.right<2||client.bottom<2)return false;s.wait();
    if(!s.swap||s.attached_window!=window||s.window_width!=static_cast<UINT>(client.right)||s.window_height!=static_cast<UINT>(client.bottom)||s.rebuild)s.create_swap(window);
    UINT index=0;const auto result=s.vkAcquireNextImageKHR(s.device,s.swap,timeout_ns,s.acquired,VK_NULL_HANDLE,&index);if(result==VK_ERROR_OUT_OF_DATE_KHR){s.rebuild=true;return false;}if(result!=VK_SUBOPTIMAL_KHR)check(result,"Acquire Vulkan backbuffer");else s.rebuild=true;
    s.convert(frame);s.begin();s.draw(s.images.at(index),s.extent.width,s.extent.height,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);s.submit(s.acquired,s.rendered.at(index));
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&s.rendered[index];present.swapchainCount=1;present.pSwapchains=&s.swap;present.pImageIndices=&index;const auto shown=s.vkQueuePresentKHR(s.queue,&present);
    if(shown==VK_ERROR_OUT_OF_DATE_KHR||shown==VK_SUBOPTIMAL_KHR)s.rebuild=true;else check(shown,"Vulkan presentation failed");s.wait();return shown!=VK_ERROR_OUT_OF_DATE_KHR;
}
Frame VulkanRenderer::diagnostic(const Frame& frame,unsigned width,unsigned height){
    auto& s=*state_;s.wait();VkImage target=VK_NULL_HANDLE;VkDeviceMemory target_memory=VK_NULL_HANDLE,readback_memory=VK_NULL_HANDLE;VkBuffer readback=VK_NULL_HANDLE;
    auto cleanup=[&]{s.vkDeviceWaitIdle(s.device);if(readback)s.vkDestroyBuffer(s.device,readback,nullptr);if(readback_memory)s.vkFreeMemory(s.device,readback_memory,nullptr);if(target)s.vkDestroyImage(s.device,target,nullptr);if(target_memory)s.vkFreeMemory(s.device,target_memory,nullptr);};
    try{
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=format;image.extent={width,height,1};image.mipLevels=1;image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(s.vkCreateImage(s.device,&image,nullptr,&target),"Create Vulkan diagnostic target");VkMemoryRequirements requirements{};s.vkGetImageMemoryRequirements(s.device,target,&requirements);VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=s.memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);check(s.vkAllocateMemory(s.device,&allocation,nullptr,&target_memory),"Allocate Vulkan diagnostic target");check(s.vkBindImageMemory(s.device,target,target_memory,0),"Bind Vulkan diagnostic target");
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=static_cast<VkDeviceSize>(width)*height*4;buffer.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;buffer.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(s.vkCreateBuffer(s.device,&buffer,nullptr,&readback),"Create Vulkan diagnostic readback");s.vkGetBufferMemoryRequirements(s.device,readback,&requirements);allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=s.memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);check(s.vkAllocateMemory(s.device,&allocation,nullptr,&readback_memory),"Allocate Vulkan diagnostic readback");check(s.vkBindBufferMemory(s.device,readback,readback_memory,0),"Bind Vulkan diagnostic readback");
        s.convert(frame);s.begin();s.draw(target,width,height,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};s.vkCmdCopyImageToBuffer(s.commands,target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);s.submit(VK_NULL_HANDLE,VK_NULL_HANDLE);s.wait();
        Frame result{width,height,std::vector<std::uint8_t>(static_cast<size_t>(buffer.size))};void* mapped=nullptr;check(s.vkMapMemory(s.device,readback_memory,0,buffer.size,0,&mapped),"Map Vulkan diagnostic result");std::memcpy(result.bgra.data(),mapped,result.bgra.size());s.vkUnmapMemory(s.device,readback_memory);cleanup();return result;
    }catch(...){cleanup();throw;}
}
}
