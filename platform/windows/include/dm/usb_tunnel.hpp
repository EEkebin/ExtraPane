#pragma once
#include <dm/network.hpp>
#include <extrapane/usb_tunnel.hpp>
#include <dm/tls.hpp>
#include <atomic>
#include <string>
#include <functional>
namespace dm::usb {
// Requires completed TLS/access admission AND a resource grant. This function
// never admits peers or exposes a listener. local must connect two127.0.0.1 endpoints.
// importer starts turns; exporter must supply its exact locally selected bus ID.
// Both borrowed streams stay alive until pump returns and its cancel watcher joins.
// stop is forced cancellation. On the source, return_request queues a framed
// eject request; on the receiver it receives requests. notice is receiver
// output and status notifies the source without ending transport. The receiver's
// ejected callback may return true only after successful native safe eject; a
// local socket error alone never provides that proof.
void pump_tunnel(security::TlsStream& remote,net::TcpStream& local,bool importer,
                 const std::string& exporter_bus,std::atomic_bool& stop,bool keep_tls_open=false,
                 std::atomic_bool* return_request=nullptr,std::atomic<extrapane::usb::tunnel::Control>* notice=nullptr,
                 const std::function<void(extrapane::usb::tunnel::Control)>& status={},
                 const std::function<bool(bool local_failure)>& ejected={});
}
