#pragma once
#include "lemon/backends/backend_registry.h"
#include "lemon/backends/llamacpp/llamacpp_server.h"
#include "lemon/model_manager.h"

#include <memory>
#include <set>
#include <string>

namespace lemon {
namespace backends {

// The 1bit engine as an executor. `1bit serve` already speaks the OpenAI API
// and answers under the name it is given (--alias), so LlamaCppServer's
// forwarding is used unchanged; only load() differs.
class OnebitServer : public LlamaCppServer {
public:
    using LlamaCppServer::LlamaCppServer;

    void load(const std::string& model_name,
              const ModelInfo& model_info,
              const RecipeOptions& options,
              bool do_not_upgrade = false) override;

    // This Lemonade's API base URL: `1bit serve --pm` (recipe option onebit_pm) asks it to run
    // the expert models it delegates to (--lemonade-url).
    void set_lemonade_url(std::string url) { lemonade_url_ = std::move(url); }

private:
    std::string lemonade_url_;
};

namespace onebit {

// The 1bit binary: $LEMONADE_ONEBIT_BIN, else `1bit` on PATH. Requests cannot choose it.
std::string resolve_binary();

// onebit_args takes only these 1bit serve tuning flags (a flag may carry `=value`).
// Flags that set an address, a port, another server's URL, or a binary, library,
// model or file path to load are refused: Lemonade sets the ones it needs itself.
const std::set<std::string>& allowed_arg_flags();

// "" when every flag in onebit_args is allowed, else the reason it is refused.
std::string validate_args(const std::string& onebit_args);

std::unique_ptr<WrappedServer> create(const BackendContext& ctx);
const BackendSpec* spec();
const BackendOps* ops();
constexpr uint32_t capabilities() { return capability_mask_of<OnebitServer>(); }

}  // namespace onebit

}  // namespace backends
}  // namespace lemon
