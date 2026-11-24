#include "lemon/heavy_work_hold.h"
#include "lemon/server/api_route.h"
#include "lemon/server/route_registry.h"

namespace lemon {
namespace {

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

const RouteError kHoldActive{409, "", "a hold is already active; release it first"};
const RouteError kHoldNotStarted{503, "", "the model slots could not be taken"};
const RouteError kUnknownHold{404, "", "`hold_id` is not the active hold, or the last one"};

const RouteArg kReason{"reason", ArgIn::JsonBody, {{"type", "string"}, {"maxLength", 200}}, false, Support::Available,
                       "What the slots are freed for, shown in `GET /v1/hold` and the log."};
const RouteArg kMaxSeconds{"max_seconds",
                           ArgIn::JsonBody,
                           {{"type", "integer"}, {"minimum", 1}, {"maximum", HeavyWorkHold::kMaxSecondsLimit}},
                           false,
                           Support::Available,
                           "Releases the hold by itself after this many seconds, so a caller that died cannot "
                           "keep every model out. Default 3600."};
const RouteArg kWaitSeconds{"wait_seconds",
                            ArgIn::JsonBody,
                            {{"type", "integer"}, {"minimum", 1}, {"maximum", HeavyWorkHold::kMaxWaitSeconds}},
                            false,
                            Support::Available,
                            "How long to wait for in-flight requests (or a running job) to let go of the model slots. "
                            "On the deadline the attempt is cancelled, nothing is unloaded, and the answer is 503, so a "
                            "hold never starts after its caller has given up. Default 300."};
const RouteArg kHoldId{"hold_id", ArgIn::JsonBody, {{"type", "string"}, {"minLength", 1}}, true, Support::Available,
                       "The `hold_id` that `POST /v1/hold` returned."};

ordered_json status_schema() {
    return ordered_json::parse(R"({
        "type": "object",
        "required": ["active"],
        "properties": {
            "active": {"type": "boolean", "description": "Whether a hold keeps the model slots free."},
            "hold_id": {"type": "string", "description": "The active hold's id."},
            "reason": {"type": "string", "description": "The `reason` the hold was taken with."},
            "since_s": {"type": "number", "description": "Seconds since the models were unloaded."},
            "max_seconds": {"type": "integer", "description": "When the hold releases itself."},
            "phase": {"enum": ["starting", "holding", "releasing"], "description": "`starting` while in-flight requests finish, `holding` while the slots are free, `releasing` while the models come back."},
            "unloaded": {"type": "array", "items": {"type": "string"}, "description": "The models the hold unloaded and will load again."}
        }
    })");
}

class HoldRoute : public ApiRoute {
public:
    using ApiRoute::ApiRoute;

    RouteSpec spec() const override {
        RouteSpec s;
        s.id = "lemonade.hold";
        s.methods = {"POST"};
        s.paths = {"hold"};
        s.summary = "Free every model slot for heavy work";
        s.experimental = true;
        s.description =
            "Waits for in-flight requests to finish, unloads every loaded model and keeps the slots free until "
            "[`POST /v1/hold/release`](#post-v1holdrelease) or `max_seconds`. Requests that arrive meanwhile wait "
            "instead of loading a model, and run once the same models are back with the same options. Use it "
            "around a build, a quantization or a benchmark that needs the GPU, the NPU or the memory.";
        s.request_format = RequestFormat::OptionalJson;
        s.args = {kReason, kMaxSeconds, kWaitSeconds};
        RouteResponse response;
        response.format = ResponseFormat::Json;
        response.schema = ordered_json::parse(R"({
            "type": "object",
            "required": ["hold_id", "reason", "max_seconds", "unloaded"],
            "properties": {
                "hold_id": {"type": "string", "description": "Pass it to `POST /v1/hold/release`."},
                "reason": {"type": "string", "description": "The `reason` given."},
                "max_seconds": {"type": "integer", "description": "When the hold releases itself."},
                "unloaded": {"type": "array", "items": {"type": "string"}, "description": "The models that were unloaded."}
            }
        })");
        s.responses = {response};
        RouteExample example;
        example.format = ResponseFormat::Json;
        example.args = {{"reason", "quantize a checkpoint"}, {"max_seconds", 1}};
        s.examples = {example};
        s.errors = {kBodyNotJson, kArgInvalid, kHoldActive, kHoldNotStarted};
        return s;
    }

    void handle(RouteRequest& req, httplib::Response& res) override {
        const std::string reason = req.has(kReason) ? req.arg(kReason).get<std::string>() : std::string("heavy work");
        const int max_seconds = req.has(kMaxSeconds) ? req.arg(kMaxSeconds).get<int>() : 3600;
        const int wait_seconds =
            req.has(kWaitSeconds) ? req.arg(kWaitSeconds).get<int>() : HeavyWorkHold::kDefaultWaitSeconds;
        json out;
        std::string error;
        if (!ctx_.hold->begin(reason, max_seconds, out, error, wait_seconds)) {
            const bool active = ctx_.hold->status().value("active", false);
            write_plain_error(res, active ? kHoldActive : kHoldNotStarted, error);
            return;
        }
        res.set_content(out.dump(), "application/json");
    }
};

class HoldReleaseRoute : public ApiRoute {
public:
    using ApiRoute::ApiRoute;

    RouteSpec spec() const override {
        RouteSpec s;
        s.id = "lemonade.hold_release";
        s.methods = {"POST"};
        s.paths = {"hold/release"};
        s.summary = "Bring the held models back";
        s.experimental = true;
        s.description =
            "Loads the models a hold unloaded again, with the options they had, then lets the waiting requests run. "
            "Answers once they are loaded. Releasing a hold that already expired answers how it ended.";
        s.request_format = RequestFormat::Json;
        s.args = {kHoldId};
        RouteResponse response;
        response.format = ResponseFormat::Json;
        response.schema = ordered_json::parse(R"({
            "type": "object",
            "required": ["hold_id", "reloaded", "failed", "expired"],
            "properties": {
                "hold_id": {"type": "string", "description": "The released hold."},
                "reloaded": {"type": "array", "items": {"type": "string"}, "description": "Models loaded again."},
                "failed": {"type": "array", "items": {"type": "object", "properties": {"model": {"type": "string", "description": "The model."}, "error": {"type": "string", "description": "Why it did not load."}}}, "description": "Models that could not be loaded again; a later request loads them as usual."},
                "expired": {"type": "boolean", "description": "Whether `max_seconds` ended the hold before this call."}
            }
        })");
        s.responses = {response};
        RouteExample example;
        example.format = ResponseFormat::Json;
        example.setup = {{"lemonade.hold", ""}};
        example.args = {{"hold_id", "$lemonade.hold/hold_id"}};
        s.examples = {example};
        s.errors = {kBodyNotJson, kArgMissing, kArgInvalid, kUnknownHold};
        return s;
    }

    void handle(RouteRequest& req, httplib::Response& res) override {
        json out;
        if (!ctx_.hold->release(req.arg(kHoldId).get<std::string>(), out)) {
            write_plain_error(res, kUnknownHold, "no such hold");
            return;
        }
        res.set_content(out.dump(), "application/json");
    }
};

class HoldStatusRoute : public ApiRoute {
public:
    using ApiRoute::ApiRoute;

    RouteSpec spec() const override {
        RouteSpec s;
        s.id = "lemonade.hold_status";
        s.methods = {"GET"};
        s.paths = {"hold"};
        s.summary = "Whether a hold keeps the model slots free";
        s.experimental = true;
        s.description = "The active hold, if any: why, since when, and which models it will bring back.";
        RouteResponse response;
        response.format = ResponseFormat::Json;
        response.schema = status_schema();
        s.responses = {response};
        RouteExample example;
        example.format = ResponseFormat::Json;
        example.args = json::object();
        s.examples = {example};
        return s;
    }

    void handle(RouteRequest&, httplib::Response& res) override {
        res.set_content(ctx_.hold->status().dump(), "application/json");
    }
};

} // namespace

void register_lemonade_hold_routes(RouteRegistry& registry, ServerContext& ctx) {
    // The docs generator runs examples in this order and has no teardown: the release example opens and closes its
    // own hold, and the hold example's hold, which nothing releases, ends after its one second.
    registry.add(std::make_unique<HoldStatusRoute>(ctx));
    registry.add(std::make_unique<HoldReleaseRoute>(ctx));
    registry.add(std::make_unique<HoldRoute>(ctx));
}

} // namespace lemon
