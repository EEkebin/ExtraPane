#pragma once
#include <dm/network.hpp>
#include <dm/tls.hpp>
#include <atomic>
#include <string>
namespace dm::usb {
// Requires completed TLS/access admission AND a resource grant. This function
// never admits peers or exposes a listener. local must connect two127.0.0.1 endpoints.
// importer starts turns; exporter must supply its exact locally selected bus ID.
// Both borrowed streams stay alive until pump returns and its cancel watcher joins.
void pump_tunnel(security::TlsStream& remote,net::TcpStream& local,bool importer,
                 const std::string& exporter_bus,std::atomic_bool& stop,bool keep_tls_open=false);
}
