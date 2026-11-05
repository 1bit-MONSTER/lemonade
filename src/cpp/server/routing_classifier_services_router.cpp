#include "lemon/routing_classifier_services.h"

#include "lemon/backends/onebit/laya_client.h"
#include "lemon/router.h"

#include <lemon/models/model_info.h>
#include <map>
#include <mutex>
#include <utility>

namespace lemon {

ClassifierServices make_router_classifier_services(
    Router& router,
    EnsureClassifierModelLoaded ensure_loaded) {
    ClassifierServices services = make_classifier_services_from_router_calls(
        [&router](const json& request) { return router.embeddings(request); },
        [&router](const json& request) { return router.chat_completion(request); },
        std::move(ensure_loaded),
        [&router](const json& request) { return router.classify(request); },
        [&router](const std::string& model) { return router.get_model_type(model); });
    // The engine's resident router daemon (`1bit route --serve`, docs/laya-first.md): the `laya`
    // classifier type asks it for the request class instead of loading a model of its own. A daemon
    // that is absent or dead answers ok=false, so the condition applies on_error rather than the
    // request failing.
    services.laya = [](const std::string& text) {
        const backends::onebit::LayaAnswer answer = backends::onebit::laya_decide(text);
        LayaDecision decision;
        decision.ok = answer.ok;
        decision.label = answer.label;
        decision.confidence = answer.confidence;
        decision.device = answer.device;
        decision.decision_ms = answer.decision_ms;
        return decision;
    };
    return services;
}

CostServices make_router_cost_services(Router& router) {
    // Process-lifetime memo: cost metadata is effectively static per model name
    // for a running lemond. Avoids a registry/build_cache hit on every routed
    // request. Not invalidated on mid-process catalog rebuild (prices rarely
    // change without a restart); revisit if ModelManager gains a generation
    // counter consumers can subscribe to.
    static std::mutex cache_mu;
    static std::map<std::string, CostInfo> cache;

    CostServices services;
    services.cost_of = [&router](const std::string& candidate) -> CostInfo {
        {
            std::lock_guard<std::mutex> lock(cache_mu);
            auto it = cache.find(candidate);
            if (it != cache.end()) {
                return it->second;
            }
        }

        CostInfo info;
        std::optional<ModelInfo> model = router.try_get_model_info(candidate);
        if (model) {
            const std::optional<double> typed_input =
                model->cost_input_per_million >= 0.0
                    ? std::optional<double>{model->cost_input_per_million}
                    : std::nullopt;
            const std::optional<double> typed_output =
                model->cost_output_per_million >= 0.0
                    ? std::optional<double>{model->cost_output_per_million}
                    : std::nullopt;
            info = resolve_cost_info(typed_input, typed_output, model->extras);
        }

        std::lock_guard<std::mutex> lock(cache_mu);
        auto [it, inserted] = cache.emplace(candidate, info);
        (void)inserted;
        return it->second;
    };
    return services;
}

} // namespace lemon
