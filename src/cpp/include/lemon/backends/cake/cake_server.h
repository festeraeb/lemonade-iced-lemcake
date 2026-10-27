#pragma once

#include "lemon/backends/backend_registry.h"
#include "lemon/wrapped_server.h"

namespace lemon { namespace backends {

class CakeServer : public WrappedServer, public ICompletionServer {
public:
    CakeServer(const std::string& log_level,
               ModelManager* model_manager,
               BackendManager* backend_manager);
    ~CakeServer() override;

    void load(const std::string& model_name,
              const ModelInfo& model_info,
              const RecipeOptions& options,
              bool do_not_upgrade = false) override;

    void unload() override;

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
}

}}  // namespace lemon::backends
