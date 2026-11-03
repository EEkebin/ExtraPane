#pragma once
#include "transport.hpp"
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace dm::mac {
struct LocalAddress { std::string interface, address; };
inline std::vector<LocalAddress> localAddresses(){
    ifaddrs* list=nullptr;
    if(getifaddrs(&list))throw std::runtime_error("Cannot enumerate local network addresses");
    struct Cleanup {ifaddrs* list;~Cleanup(){freeifaddrs(list);}} cleanup{list};
    std::vector<LocalAddress> result;
    for(auto a=list;a;a=a->ifa_next){
        if(!a->ifa_addr || a->ifa_addr->sa_family!=AF_INET ||
           !(a->ifa_flags&IFF_UP) || !(a->ifa_flags&IFF_RUNNING) ||
           (a->ifa_flags&IFF_LOOPBACK) || !a->ifa_name || std::strncmp(a->ifa_name,"en",2))continue;
        char ip[INET_ADDRSTRLEN]{};
        if(!inet_ntop(AF_INET,&reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr,ip,sizeof(ip)) || !ipv4(ip))continue;
        if(std::none_of(result.begin(),result.end(),[&](const auto& entry){return entry.address==ip;}))
            result.push_back({a->ifa_name,ip});
    }
    return result;
}
}
