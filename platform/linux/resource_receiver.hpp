#pragma once
#include "tls.hpp"
#include <extrapane/session.hpp>
#include <QString>
#include <functional>
#include <span>

namespace dm::linux_client {
// Called only after transport admission and resource-prefix classification.
// No display/portal is allocated for camera or microphone connections.
QString resourceReceiverUnavailable(extrapane::ResourceKind kind);
void receiveResource(Tls& stream, extrapane::ResourceKind kind, std::span<const std::uint8_t> prefix,
                     const std::function<bool()>& cancelled, const std::function<void(QString)>& report);
}
