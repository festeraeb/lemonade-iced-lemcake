#include <lemon/nexus_tier3.h>

#include <lemon/utils/aixlog.hpp>
#include <lemon/version.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace lemon {

namespace {

// --- The slice of the Nexus C ABI this file uses (lemonade_nexus.h) --------
// Resolved by name at run time; the SDK keeps these stable across releases.
struct ln_client;
struct ln_identity;
using ln_error_t = int32_t;
constexpr ln_error_t LN_OK = 0;

struct SdkApi {
    ln_client* (*create)(const char*, uint16_t) = nullptr;
    ln_client* (*create_tls)(const char*, uint16_t) = nullptr;
    void (*destroy)(ln_client*) = nullptr;
    ln_identity* (*identity_generate)() = nullptr;
    ln_identity* (*identity_load)(const char*) = nullptr;
    ln_error_t (*identity_save)(const ln_identity*, const char*) = nullptr;
    void (*identity_destroy)(ln_identity*) = nullptr;
    ln_error_t (*set_identity)(ln_client*, const ln_identity*) = nullptr;
    ln_error_t (*set_link_token)(ln_client*, const char*) = nullptr;
    ln_error_t (*set_inference_node)(ln_client*, int) = nullptr;
    ln_error_t (*set_device_name)(ln_client*, const char*) = nullptr;
    ln_error_t (*join_network)(ln_client*, const char*, const char*, char**) = nullptr;
    ln_error_t (*mesh_enable)(ln_client*) = nullptr;
    ln_error_t (*mesh_disable)(ln_client*) = nullptr;
    int (*mesh_is_active)(ln_client*) = nullptr;
    ln_error_t (*mesh_expose_service)(ln_client*, uint16_t, const char*) = nullptr;
    ln_error_t (*mesh_service_set_catalog)(ln_client*, uint16_t, const char*) = nullptr;
    void (*free)(char*) = nullptr;
};

void* open_library(const std::string& path, std::string& error) {
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path.c_str());
    if (!h) error = "LoadLibrary failed for " + path;
    return reinterpret_cast<void*>(h);
#else
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) error = dlerror() ? dlerror() : ("dlopen failed for " + path);
    return h;
#endif
}

void* symbol(void* lib, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

void close_library(void* lib) {
    if (!lib) return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(lib));
#else
    dlclose(lib);
#endif
}

bool resolve(void* lib, SdkApi& api, std::string& missing) {
#define LN_SYM(field, name) \
    api.field = reinterpret_cast<decltype(api.field)>(symbol(lib, name)); \
    if (!api.field) { missing = name; return false; }
    LN_SYM(create, "ln_create");
    LN_SYM(create_tls, "ln_create_tls");
    LN_SYM(destroy, "ln_destroy");
    LN_SYM(identity_generate, "ln_identity_generate");
    LN_SYM(identity_load, "ln_identity_load");
    LN_SYM(identity_save, "ln_identity_save");
    LN_SYM(identity_destroy, "ln_identity_destroy");
    LN_SYM(set_identity, "ln_set_identity");
    LN_SYM(set_link_token, "ln_set_link_token");
    LN_SYM(set_inference_node, "ln_set_inference_node");
    LN_SYM(set_device_name, "ln_set_device_name");
    LN_SYM(join_network, "ln_join_network");
    LN_SYM(mesh_enable, "ln_mesh_enable");
    LN_SYM(mesh_disable, "ln_mesh_disable");
    LN_SYM(mesh_is_active, "ln_mesh_is_active");
    LN_SYM(mesh_expose_service, "ln_mesh_expose_service");
    LN_SYM(mesh_service_set_catalog, "ln_mesh_service_set_catalog");
    LN_SYM(free, "ln_free");
#undef LN_SYM
    return true;
}

std::string default_library_name() {
#if defined(_WIN32)
    return "lemonade_nexus_sdk.dll";
#elif defined(__APPLE__)
    return "liblemonade_nexus_sdk.dylib";
#else
    return "liblemonade_nexus_sdk.so";
#endif
}

std::string hostname() {
    char buf[256] = {0};
#if defined(_WIN32)
    DWORD n = sizeof(buf);
    if (!GetComputerNameA(buf, &n)) return "lemonade";
#else
    if (gethostname(buf, sizeof(buf) - 1) != 0) return "lemonade";
#endif
    return buf;
}

struct Settings {
    std::string server_host;
    uint16_t server_port{9100};
    bool tls{true};
    std::string sdk_library;
    std::string data_root;
    std::string link_token;
    std::string device_name;
    uint16_t vport{13305};
    std::string catalog_file;
};

bool read_settings(const json& nexus, const std::string& config_dir, Settings& s, std::string& why) {
    const std::string server = nexus.value("server", "");
    const auto colon = server.rfind(':');
    if (server.empty() || colon == std::string::npos) {
        why = "nexus.server must be \"<certificate-fqdn>:<port>\"";
        return false;
    }
    s.server_host = server.substr(0, colon);
    try {
        s.server_port = static_cast<uint16_t>(std::stoi(server.substr(colon + 1)));
    } catch (...) {
        why = "nexus.server port is not a number";
        return false;
    }
    s.tls = nexus.value("tls", true);
    s.sdk_library = nexus.value("sdk_library", default_library_name());
    s.data_root = nexus.value("data_root", (std::filesystem::path(config_dir) / "nexus").string());
    s.link_token = nexus.value("link_token", "");
    if (const char* env = std::getenv("LEMONADE_NEXUS_LINK_TOKEN"); env && *env) s.link_token = env;
    s.device_name = nexus.value("device_name", "");
    if (s.device_name.empty()) s.device_name = hostname();
    s.vport = static_cast<uint16_t>(nexus.value("vport", 13305));
    s.catalog_file = nexus.value("catalog_file", "");
    return true;
}

}  // namespace

json NexusTier3::default_catalog(const std::string& lemond_version) {
    // Family "inference": what a client needs to list models, check health and
    // chat. Everything else lemond serves (load/unload, pull, config, system
    // info) is "management" and needs that permission or admin.
    return json{
        {"version", 1},
        {"app", {{"name", "lemonade"}, {"version", lemond_version}}},
        {"families",
         json::array({
             json{{"name", "inference"},
                  {"routes",
                   json::array({
                       json{{"methods", json::array({"GET"})}, {"path", "/api/v1/models"}},
                       json{{"methods", json::array({"GET"})}, {"path", "/api/v1/health"}},
                       json{{"methods", json::array({"POST"})}, {"path", "/api/v1/chat/completions"}},
                       json{{"methods", json::array({"POST"})}, {"path", "/api/v1/completions"}},
                       json{{"methods", json::array({"POST"})}, {"path", "/api/v1/embeddings"}},
                       json{{"methods", json::array({"POST"})}, {"path", "/api/v1/responses"}},
                       json{{"methods", json::array({"GET"})}, {"path", "/v1/models"}},
                       json{{"methods", json::array({"POST"})}, {"path", "/v1/chat/completions"}},
                       json{{"methods", json::array({"POST"})}, {"path", "/v1/completions"}},
                       json{{"methods", json::array({"POST"})}, {"path", "/v1/embeddings"}},
                   })}},
             json{{"name", "management"},
                  {"routes",
                   json::array({
                       json{{"methods", json::array({"GET", "POST", "DELETE"})}, {"prefix", "/api/v1/"}},
                       json{{"methods", json::array({"GET", "POST", "DELETE"})}, {"prefix", "/v1/"}},
                   })}},
         })},
    };
}

struct NexusTier3::Impl {
    Settings settings;
    int lemond_port{13305};
    json catalog;
    void* lib{nullptr};
    SdkApi api;
    std::thread worker;
    std::atomic<bool> stop{false};
    mutable std::mutex mtx;
    std::string status{"loading-sdk"};

    void set_status(std::string s) {
        std::lock_guard<std::mutex> lock(mtx);
        status = std::move(s);
    }

    void run() {
        std::string error;
        lib = open_library(settings.sdk_library, error);
        if (!lib) {
            LOG(WARNING) << "Nexus: SDK library not loaded (" << error
                         << "); lemond runs without a mesh." << std::endl;
            set_status("failed: " + error);
            return;
        }
        std::string missing;
        if (!resolve(lib, api, missing)) {
            LOG(WARNING) << "Nexus: SDK library lacks symbol " << missing
                         << "; lemond runs without a mesh." << std::endl;
            set_status("failed: missing " + missing);
            return;
        }

        std::filesystem::create_directories(settings.data_root);
        const std::string identity_path =
            (std::filesystem::path(settings.data_root) / "identity.json").string();
        ln_identity* identity = api.identity_load(identity_path.c_str());
        if (!identity) {
            identity = api.identity_generate();
            if (!identity || api.identity_save(identity, identity_path.c_str()) != LN_OK) {
                LOG(ERROR) << "Nexus: cannot create or save the device identity at "
                           << identity_path << std::endl;
                set_status("failed: identity");
                return;
            }
            LOG(INFO) << "Nexus: new device identity at " << identity_path << std::endl;
        }

        ln_client* client = settings.tls ? api.create_tls(settings.server_host.c_str(), settings.server_port)
                                         : api.create(settings.server_host.c_str(), settings.server_port);
        if (!client) {
            LOG(ERROR) << "Nexus: cannot create the SDK client" << std::endl;
            api.identity_destroy(identity);
            set_status("failed: client");
            return;
        }
        api.set_identity(client, identity);
        api.set_device_name(client, settings.device_name.c_str());
        // Tier 3 is decided at endpoint creation: before the join, always.
        api.set_inference_node(client, 1);
        if (!settings.link_token.empty()) api.set_link_token(client, settings.link_token.c_str());

        const std::string catalog_text = catalog.dump();
        const std::string target = "tcp:127.0.0.1:" + std::to_string(lemond_port);
        bool joined = false;
        auto backoff = std::chrono::seconds(2);
        set_status("joining");
        while (!stop.load()) {
            if (!joined) {
                char* out = nullptr;
                const ln_error_t rc = api.join_network(client, "lemonade", "", &out);
                std::string reply = out ? out : "";
                if (out) api.free(out);
                if (rc == LN_OK) {
                    joined = true;
                    backoff = std::chrono::seconds(2);
                    api.set_link_token(client, "");  // single-use; never replayed
                    LOG(INFO) << "Nexus: joined as a Tier 3 inference node: " << reply << std::endl;
                    if (api.mesh_enable(client) != LN_OK) {
                        LOG(WARNING) << "Nexus: mesh did not enable" << std::endl;
                    }
                    if (api.mesh_service_set_catalog(client, settings.vport, catalog_text.c_str()) != LN_OK) {
                        LOG(WARNING) << "Nexus: service catalog refused on vport " << settings.vport << std::endl;
                    }
                    if (api.mesh_expose_service(client, settings.vport, target.c_str()) == LN_OK) {
                        LOG(INFO) << "Nexus: exposing " << target << " at mesh :" << settings.vport << std::endl;
                        set_status("running");
                    } else {
                        LOG(WARNING) << "Nexus: expose of " << target << " failed" << std::endl;
                        set_status("degraded");
                    }
                } else {
                    LOG(WARNING) << "Nexus: join failed (rc=" << rc << ") " << reply
                                 << "; retry in " << backoff.count() << "s" << std::endl;
                    for (int i = 0; i < backoff.count() * 10 && !stop.load(); ++i)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    backoff = std::min(backoff * 2, std::chrono::seconds(60));
                    continue;
                }
            } else if (api.mesh_is_active(client) == 0) {
                set_status("degraded");
            } else {
                set_status("running");
            }
            for (int i = 0; i < 50 && !stop.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (joined) api.mesh_disable(client);
        api.destroy(client);
        api.identity_destroy(identity);
        set_status("stopped");
    }
};

NexusTier3::NexusTier3(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<NexusTier3> NexusTier3::start_from_config(const json& config,
                                                          const std::string& config_dir,
                                                          int lemond_port) {
    try {
        if (!config.is_object() || !config.contains("nexus") || !config["nexus"].is_object() ||
            !config["nexus"].value("enabled", false)) {
            return nullptr;
        }
        auto impl = std::make_unique<Impl>();
        std::string why;
        if (!read_settings(config["nexus"], config_dir, impl->settings, why)) {
            LOG(WARNING) << "Nexus: not started: " << why << std::endl;
            return nullptr;
        }
        impl->lemond_port = lemond_port;
        impl->catalog = default_catalog(LEMON_VERSION_STRING);
        if (!impl->settings.catalog_file.empty()) {
            std::ifstream in(impl->settings.catalog_file);
            json parsed = in ? json::parse(in, nullptr, false) : json();
            if (parsed.is_discarded() || !parsed.is_object()) {
                LOG(WARNING) << "Nexus: catalog_file is not a JSON object; using the default catalog" << std::endl;
            } else {
                impl->catalog = parsed;
            }
        }
        LOG(INFO) << "Nexus: Tier 3 inference node enabled (server " << impl->settings.server_host
                  << ":" << impl->settings.server_port << ", vport " << impl->settings.vport
                  << " -> 127.0.0.1:" << lemond_port << ")" << std::endl;
        Impl* raw = impl.get();
        impl->worker = std::thread([raw] { raw->run(); });
        return std::unique_ptr<NexusTier3>(new NexusTier3(std::move(impl)));
    } catch (const std::exception& e) {
        LOG(WARNING) << "Nexus: not started: " << e.what() << std::endl;
        return nullptr;
    }
}

NexusTier3::~NexusTier3() {
    if (!impl_) return;
    impl_->stop.store(true);
    if (impl_->worker.joinable()) impl_->worker.join();
    close_library(impl_->lib);
}

std::string NexusTier3::status() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->status;
}

}  // namespace lemon
