// onebit_server.cpp: the 1bit engine (`1bit serve`) as a Lemonade backend.
#include "lemon/backends/onebit/onebit_server.h"

#include "lemon/backends/backend_ops.h"
#include "lemon/backends/onebit/onebit.h"
#include "lemon/utils/process_manager.h"

#include <lemon/utils/aixlog.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lemon {
namespace backends {

namespace {

// Checkpoints are GGUF files, so model management (variant resolution, GGUF
// metadata, cache validation) is llamacpp's; only the runtime differs.
class OnebitOps : public BackendOps {
public:
    void populate_metadata(ModelInfo& info, const BackendOpsContext& ctx) const override {
        llamacpp::ops()->populate_metadata(info, ctx);
    }
    std::string resolve_checkpoint_path(const ModelInfo& info,
                                        const CheckpointResolveContext& ctx) const override {
        return llamacpp::ops()->resolve_checkpoint_path(info, ctx);
    }
    std::string find_imported_checkpoint(const std::string& import_dir) const override {
        return llamacpp::ops()->find_imported_checkpoint(import_dir);
    }
    std::string validate_registration_checkpoint(const std::string& checkpoint) const override {
        return llamacpp::ops()->validate_registration_checkpoint(checkpoint);
    }
    std::string validate_checkpoint_file(const std::string& resolved_path) const override {
        return llamacpp::ops()->validate_checkpoint_file(resolved_path);
    }
    // `1bit version` prints "1bit <version>".
    std::string resolve_version(const std::string&, const std::string& file_version) const override {
        if (!file_version.empty() && file_version != "unknown") return file_version;
        const std::string bin = onebit::resolve_binary("");
        std::array<char, 128> buf{};
        std::string out;
        if (FILE* p = ::popen((bin + " version 2>/dev/null").c_str(), "r")) {
            while (std::fgets(buf.data(), int(buf.size()), p)) out += buf.data();
            ::pclose(p);
        }
        const auto sp = out.find(' ');
        std::string v = sp == std::string::npos ? "" : out.substr(sp + 1);
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
        return v.empty() ? "unknown" : v;
    }
    InstallCheck check_install(const std::string&, bool) const override {
        const std::string bin = onebit::resolve_binary("");
        const bool found = std::system((std::string("command -v ") + bin + " >/dev/null 2>&1").c_str()) == 0;
        return {found, found ? "" : "the 1bit binary was not found (github.com/1bit-MONSTER/engine)"};
    }
};

std::vector<std::string> split_args(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string a; in >> a;) out.push_back(a);
    return out;
}

}  // namespace

void OnebitServer::load(const std::string& model_name,
                        const ModelInfo& model_info,
                        const RecipeOptions& options,
                        bool do_not_upgrade) {
    (void)do_not_upgrade;
    LOG(INFO, "1bit") << "Loading model: " << model_name << std::endl;

    const std::string model = model_info.resolved_path();
    if (model.empty()) {
        throw std::runtime_error("1bit model '" + model_name + "' has no resolved checkpoint");
    }
    const std::string bin = onebit::resolve_binary(options.get_option("onebit_bin"));
    std::string device = options.get_option("onebit_backend");
    if (device.empty()) device = "auto";
    if (device == "cuda") device = "zinc";  // the engine reaches NVIDIA through its ZINC build
    const int backend_port = choose_port();
    const int ctx_size = options.get_option("ctx_size");

    // --alias: replies carry Lemonade's model name, so requests and responses
    // pass through unchanged.
    std::vector<std::string> argv = {"serve", "-m", model, "--port", std::to_string(backend_port),
                                     "--device", device, "--alias", model_name};
    if (ctx_size > 0) {
        argv.push_back("--ctx-size");
        argv.push_back(std::to_string(ctx_size));
    }
    // Embedding and reranking models run in that role (llama-server --embedding / --reranking
    // on the device asked for); chat models need no flag.
    if (model_info.type == ModelType::EMBEDDING) argv.push_back("--embedding");
    if (model_info.type == ModelType::RERANKING) argv.push_back("--reranking");
    for (auto& a : split_args(options.get_option("onebit_args"))) argv.push_back(std::move(a));

    const bool inherit_output = (log_level_ == "info") || is_debug();
    set_process_handle(utils::ProcessManager::start_process(bin, argv, "", inherit_output, true, {}), bin, argv);

    if (!wait_for_ready("/health")) {
        const ProcessHandle handle = consume_process_handle_for_cleanup();
        if (has_process_handle(handle)) {
            utils::ProcessManager::stop_process(handle);
        }
        throw std::runtime_error("1bit serve failed to start for '" + model_name + "' (" + bin + ")");
    }
    LOG(DEBUG, "1bit") << "Model loaded on port " << get_backend_port() << " (" << device << ")" << std::endl;
}

namespace onebit {

std::string resolve_binary(const std::string& onebit_bin_option) {
    if (!onebit_bin_option.empty()) return onebit_bin_option;
    if (const char* env = std::getenv("LEMONADE_ONEBIT_BIN"); env && *env) return env;
    return "1bit";
}

std::unique_ptr<WrappedServer> create(const BackendContext& ctx) {
    return make_server<OnebitServer>(ctx);
}

// Installed separately: nothing for Lemonade to download.
const BackendSpec* spec() { return nullptr; }

const BackendOps* ops() { return single_ops<OnebitOps>(); }

}  // namespace onebit

}  // namespace backends
}  // namespace lemon
