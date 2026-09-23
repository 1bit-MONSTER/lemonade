#pragma once
#include "lemon/backends/backend_registry.h"
#include "lemon/backends/llamacpp/llamacpp_server.h"
#include "lemon/model_manager.h"

#include <memory>
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
};

namespace onebit {

// The 1bit binary: the onebit_bin option, else $LEMONADE_ONEBIT_BIN, else `1bit`.
std::string resolve_binary(const std::string& onebit_bin_option);

std::unique_ptr<WrappedServer> create(const BackendContext& ctx);
const BackendSpec* spec();
const BackendOps* ops();
constexpr uint32_t capabilities() { return capability_mask_of<OnebitServer>(); }

}  // namespace onebit

}  // namespace backends
}  // namespace lemon
