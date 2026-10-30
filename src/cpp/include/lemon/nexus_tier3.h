#pragma once

// Lemonade Nexus Tier 3: let lemond join a Nexus mesh as an inference node.
//
// The Nexus SDK (liblemonade_nexus_sdk.so / .dylib / .dll) is loaded at run
// time through its C ABI, so lemond has no build-time dependency on Nexus and
// a machine without the library runs exactly as before. Configuration lives
// under the "nexus" object of the server config file:
//
//   "nexus": {
//     "enabled": true,
//     "server": "nexus.example.net:9100",      // a Nexus server's certificate FQDN:port
//     "genesis_key": "<base64>",                // the network's Genesis public key (required)
//     "tls": true,                              // default true
//     "sdk_library": "liblemonade_nexus_sdk.so",// default: the platform's library name
//     "data_root": "<config dir>/nexus",        // identity.json lives here
//     "link_token": "",                         // single-use; or env LEMONADE_NEXUS_LINK_TOKEN
//     "device_name": "",                        // default: hostname
//     "vport": 13305,                           // the mesh port peers reach lemond on
//     "catalog_file": ""                        // optional; default catalog = inference family over /api/v1 and /v1
//   }
//
// Sequence (lemonade-nexus docs/Security.md "Tier 3", docs/SDK-Guide.md
// "Service catalog"): set identity, set_inference_node(1) BEFORE the join,
// join (with the link token on first run), enable mesh, register the catalog,
// expose vport -> 127.0.0.1:<lemond port>. The server records is_inference at
// endpoint creation, so this is what makes the device selectable by a mesh
// port's "auto".

#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace lemon {

using json = nlohmann::json;

class NexusTier3 {
public:
    /// Starts the Nexus client thread when config["nexus"]["enabled"] is true.
    /// Returns nullptr (and logs why) otherwise. Never throws.
    static std::unique_ptr<NexusTier3> start_from_config(const json& config,
                                                         const std::string& config_dir,
                                                         int lemond_port);
    ~NexusTier3();

    NexusTier3(const NexusTier3&) = delete;
    NexusTier3& operator=(const NexusTier3&) = delete;

    /// "loading-sdk" | "joining" | "running" | "degraded" | "stopped" | "failed: ..."
    std::string status() const;

    /// The default service catalog lemond registers on its vport.
    static json default_catalog(const std::string& lemond_version);

private:
    struct Impl;
    explicit NexusTier3(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace lemon
