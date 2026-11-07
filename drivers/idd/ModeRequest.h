#pragma once
#include <devpropdef.h>

// Versioned software-device property shared by launcher and UMDF driver. A
// request belongs to one device instance; it is never stored in a global mode.
namespace ExtraPane { namespace DisplayRequest {
static const DEVPROPKEY Property={{0xc0a5bf8e,0xbdb9,0x4cef,{0x9d,0xd1,0x47,0x31,0xc4,0x6b,0x23,0xd8}},2};
struct Request { unsigned long Version,Width,Height,Hz; };
static_assert(sizeof(Request)==16,"Display mode property ABI changed");
inline bool Valid(const Request& value){return value.Version==1&&value.Width>=2&&value.Width<=4096&&value.Height>=2&&value.Height<=4096&&!(value.Width&1)&&!(value.Height&1)&&value.Hz>=1&&value.Hz<=60&&static_cast<unsigned long long>(value.Width)*value.Height<=8847360;}
inline bool Default(const Request& value){return value.Version==1&&!value.Width&&!value.Height&&!value.Hz;}
inline void EncodeDescriptor(unsigned char* edid,const Request& value){
    for(unsigned i=108;i<126;++i)edid[i]=0;edid[111]=0xfe;edid[113]='E';edid[114]='P';edid[125]='\n';
    const char digits[]="0123456789ABCDEF";
    for(unsigned i=0;i<4;++i){edid[115+i]=digits[(value.Width>>(12-i*4))&15];edid[119+i]=digits[(value.Height>>(12-i*4))&15];}
    edid[123]=digits[(value.Hz>>4)&15];edid[124]=digits[value.Hz&15];
    unsigned sum=0;for(unsigned i=0;i<127;++i)sum+=edid[i];edid[127]=static_cast<unsigned char>(0u-sum);
}
inline bool DecodeDescriptor(const unsigned char* edid,Request& value){
    if(edid[108]||edid[109]||edid[110]||edid[111]!=0xfe||edid[112]||edid[113]!='E'||edid[114]!='P'||edid[125]!='\n'||edid[126])return false;
    unsigned sum=0;for(unsigned i=0;i<128;++i)sum+=edid[i];if(sum&255)return false;
    value={1,0,0,0};for(unsigned i=115;i<125;++i){const auto c=edid[i];const unsigned digit=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:16;if(digit>15)return false;
        auto& target=i<119?value.Width:i<123?value.Height:value.Hz;target=(target<<4)|digit;}
    return Valid(value);
}
} }
