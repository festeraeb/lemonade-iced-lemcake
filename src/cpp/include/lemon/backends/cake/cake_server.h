#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "lemon/backends/backend_registry.h"
#include "lemon/backends/backend_utils.h"
#include "lemon/wrapped_server.h"

namespace lemon {
namespace backends {

// Cake runs behind the Rust bridge (lemonade-cake-bridge), which starts `cake serve`, proxies the
// OpenAI routes and reports the runtime it actually observed. Ported to the 2026.43.x backend API
// (ServerCommand/start_server) from the 11.9 version.
class CakeServer : public WrappedServer {
public:
    // No auto-install: Cake is FAIR-licensed and fetched only after acceptance; the bridge and
    // `cake` must be on PATH.
    static InstallParams get_install_params(const std::string& backend, const std::string& version);

    CakeServer(const std::string& log_level, ModelManager* model_manager,
               BackendManager* backend_manager);

    void load(const std::string& model_name, const ModelInfo& model_info,
              const RecipeOptions& options, bool do_not_upgrade = false) override;

    json chat_completion(const json& request) override;
    json completion(const json& request) override;

    std::map<std::string, nlohmann::json> get_additional_telemetry() override;
    std::string get_additional_telemetry_url() const override;

private:
    int cake_private_port_ = 18080;
};

namespace cake {
std::unique_ptr<WrappedServer> create(const BackendContext& ctx);
const BackendSpec* spec();
const BackendOps* ops();
constexpr uint32_t capabilities() { return capability_mask_of<CakeServer>(); }
}  // namespace cake

}  // namespace backends
}  // namespace lemon
