#pragma once
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace extrapane {
// Video renderers the Windows receiver (dm-view) can present with.
inline bool known_renderer(const std::string& value){return value=="d3d11"||value=="d3d12"||value=="vulkan"||value=="opengl";}
// The per-user renderer choice saved by the app (HKCU\Software\DM\Receiver\Renderer).
// D3D11 is the default and the fallback for missing or unknown values. dm-view
// itself also falls back to D3D11 if the chosen renderer cannot start.
inline std::string saved_renderer(){
#ifdef _WIN32
    wchar_t value[32]{};DWORD size=sizeof(value);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\DM\\Receiver",L"Renderer",RRF_RT_REG_SZ,nullptr,value,&size)!=ERROR_SUCCESS)return "d3d11";
    std::string text;for(const wchar_t* c=value;*c;++c){if(*c>127)return "d3d11";text+=static_cast<char>(*c);}
    return known_renderer(text)?text:"d3d11";
#else
    return "d3d11";
#endif
}
}
