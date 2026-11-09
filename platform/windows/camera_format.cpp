#include <dm/camera.hpp>
#include <stdexcept>
namespace dm::camera {
void validate_format(Format format){
    if(!format.width||!format.height||format.width>3840||format.height>2160||format.width%2||format.height%2||!format.fps||format.fps>60)
        throw std::invalid_argument("Invalid NV12 camera configuration");
}
}
