#pragma once
#include "session.hpp"
#include <array>

namespace extrapane {
struct ResourceCapability {
    ResourceKind kind;
    Availability source;
    Availability destination;
};
using Capabilities = std::array<ResourceCapability, 4>;
inline Capabilities unavailable_capabilities() {
    return {{{ResourceKind::display,{false,"Display source unavailable"},{false,"Display receiver unavailable"}},
             {ResourceKind::camera,{false,"Camera capture unavailable"},{false,"Virtual camera unavailable"}},
             {ResourceKind::microphone,{false,"Microphone capture unavailable"},{false,"Virtual microphone unavailable"}},
             {ResourceKind::usb,{false,"USB export unavailable"},{false,"USB import unavailable"}}}};
}
// Display pixels flow server -> client; other resources flow client -> server.
inline Availability can_connect(ResourceKind kind, const Capabilities& client,
                                 const Capabilities& server) {
    const ResourceCapability* c = nullptr;
    const ResourceCapability* s = nullptr;
    for (const auto& value : client) if (value.kind == kind) {
        if (c) return {false,"Duplicate client capability"};
        c = &value;
    }
    for (const auto& value : server) if (value.kind == kind) {
        if (s) return {false,"Duplicate server capability"};
        s = &value;
    }
    if (!c || !s) return {false,"Resource capability missing"};
    const auto& source = kind == ResourceKind::display ? s->source : c->source;
    const auto& destination = kind == ResourceKind::display ? c->destination : s->destination;
    if (!source.available) return source;
    if (!destination.available) return destination;
    return {true,{}};
}
}
