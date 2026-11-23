// HeavyWorkHold over fake router hooks, and the router's exclusive section shared between two owners.
//
// The hold's promise is: in-flight work finishes, every loaded model is captured and evicted, the slots stay free
// until a release or max_seconds, and the same models come back with the options they had. Requests waiting in
// Router::load_model during the section is the router's existing behaviour; what changed there is that a second
// begin_exclusive (a job while a hold is active) now waits for the first owner instead of taking it over.
#include "lemon/heavy_work_hold.h"
#include "lemon/router.h"
#include "lemon/runtime_config.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;
using lemon::HeavyWorkHold;

static int g_failures = 0;
#define CHECK(cond, msg)                                         \
    do {                                                         \
        if (cond) {                                              \
            std::printf("ok   %s\n", msg);                       \
        } else {                                                 \
            std::printf("FAIL %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            ++g_failures;                                        \
        }                                                        \
    } while (0)

struct FakeRouter {
    std::mutex m;
    bool exclusive = false;
    int begins = 0, ends = 0;
    bool refuse_begin = false;
    bool busy_until_cancel = false;
    int cancelled = 0;
    int evicts = 0;
    std::map<std::string, json> loaded;
    std::vector<std::pair<std::string, json>> reloads;
    std::string fail_reload;

    HeavyWorkHold::Hooks hooks() {
        HeavyWorkHold::Hooks h;
        h.begin_exclusive = [this](std::atomic<bool>* cancel) {
            // busy_until_cancel: the section stays held by in-flight work until the attempt is cancelled (a router
            // that never frees up within the caller's wait)
            for (;;) {
                {
                    std::lock_guard<std::mutex> l(m);
                    if (refuse_begin) return false;
                    if (!busy_until_cancel) break;
                    if (cancel && cancel->load()) { ++cancelled; return false; }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::lock_guard<std::mutex> l(m);
            exclusive = true;
            ++begins;
            return true;
        };
        h.end_exclusive = [this] {
            std::lock_guard<std::mutex> l(m);
            exclusive = false;
            ++ends;
        };
        h.evict_all = [this] {
            std::lock_guard<std::mutex> l(m);
            ++evicts;
            auto out = loaded;
            loaded.clear();
            return out;
        };
        h.reload = [this](const std::string& model, const json& captured) {
            std::lock_guard<std::mutex> l(m);
            if (model == fail_reload) throw std::runtime_error("no memory");
            reloads.emplace_back(model, captured);
            loaded[model] = captured;
        };
        return h;
    }
};

static void test_hold_and_release() {
    FakeRouter fr;
    fr.loaded["Qwen3-0.6B-1bit"] = {{"options", {{"ctx_size", 8192}}}, {"pinned", true}};
    fr.loaded["ZAYA1-8B-1bit"] = {{"options", json::object()}, {"pinned", false}};
    HeavyWorkHold hold(fr.hooks());
    json out;
    std::string err;
    CHECK(hold.begin("quantize", 60, out, err), "begin succeeds");
    CHECK(out["unloaded"].size() == 2 && fr.loaded.empty(), "every loaded model is evicted and reported");
    CHECK(fr.exclusive, "the exclusive section stays taken while held");
    json st = hold.status();
    CHECK(st["active"] == true && st["phase"] == "holding" && st["reason"] == "quantize", "status says holding");
    json again;
    CHECK(!hold.begin("second", 60, again, err) && err.find("already active") != std::string::npos,
          "a second hold is refused while one is active");
    json rel;
    CHECK(hold.release(out["hold_id"].get<std::string>(), rel), "release succeeds");
    CHECK(rel["reloaded"].size() == 2 && rel["failed"].empty() && rel["expired"] == false,
          "both models come back, none failed, not expired");
    CHECK(fr.loaded.at("Qwen3-0.6B-1bit")["options"]["ctx_size"] == 8192 &&
              fr.loaded.at("Qwen3-0.6B-1bit")["pinned"] == true,
          "a model comes back with its options and its pin");
    CHECK(!fr.exclusive && fr.begins == 1 && fr.ends == 1, "the section is given back exactly once");
    CHECK(hold.status()["active"] == false, "status is inactive after the release");
}

// engine review of #58: a hold whose caller gives up before the section is taken must NOT start later - otherwise it
// evicts every model and holds for max_seconds with no one left to release it.
static void test_abandoned_wait_is_cancelled() {
    FakeRouter fr;
    fr.loaded["Qwen3-0.6B-1bit"] = {{"options", json::object()}, {"pinned", false}};
    fr.busy_until_cancel = true;
    HeavyWorkHold hold(fr.hooks());
    json out;
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = hold.begin("heavy", 3600, out, err, /*wait_seconds=*/1);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(!ok && err.find("within 1 s") != std::string::npos && err.find("nothing was unloaded") != std::string::npos,
          "a section that stays busy past wait_seconds is refused, and says nothing was unloaded");
    CHECK(took >= 0.9 && took < 3.0, "begin() returns at the deadline, not when the router frees up");
    CHECK(fr.cancelled == 1 && fr.evicts == 0 && fr.loaded.size() == 1 && !fr.exclusive,
          "the attempt was cancelled inside the router: no eviction, no section taken, the model still loaded");
    CHECK(hold.status()["active"] == false, "no hold is left behind");
    {
        std::lock_guard<std::mutex> l(fr.m);
        fr.busy_until_cancel = false;   // the router frees up later...
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(fr.evicts == 0 && fr.loaded.size() == 1, "...and the abandoned attempt does not start then");
    json out2;
    CHECK(hold.begin("next", 60, out2, err, 5) && out2["unloaded"].size() == 1, "a later hold works normally");
    json rel;
    CHECK(hold.release(out2["hold_id"].get<std::string>(), rel), "and releases");
    json bad;
    CHECK(!hold.begin("x", 60, bad, err, 0) && err.find("wait_seconds") != std::string::npos, "wait_seconds 0 is refused");
}

static void test_expiry() {
    FakeRouter fr;
    fr.loaded["Qwen3-0.6B-1bit"] = {{"options", json::object()}, {"pinned", false}};
    HeavyWorkHold hold(fr.hooks());
    json out;
    std::string err;
    CHECK(hold.begin("forgotten", 1, out, err), "a 1 s hold starts");
    for (int i = 0; i < 40 && hold.status()["active"] == true; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(hold.status()["active"] == false && fr.loaded.size() == 1 && !fr.exclusive,
          "max_seconds releases the hold by itself and brings the model back");
    json rel;
    CHECK(hold.release(out["hold_id"].get<std::string>(), rel) && rel["expired"] == true,
          "a late release of the expired hold answers expired=true");
    json none;
    CHECK(!hold.release("hold-999", none), "an unknown hold_id is refused");
}

static void test_failed_reload_and_refusal() {
    FakeRouter fr;
    fr.loaded["Qwen3-0.6B-1bit"] = {{"options", json::object()}, {"pinned", false}};
    fr.loaded["Qwen3.8-27B-1bit"] = {{"options", json::object()}, {"pinned", false}};
    fr.fail_reload = "Qwen3.8-27B-1bit";
    {
        HeavyWorkHold hold(fr.hooks());
        json out, rel;
        std::string err;
        hold.begin("build", 60, out, err);
        hold.release(out["hold_id"].get<std::string>(), rel);
        CHECK(rel["reloaded"].size() == 1 && rel["failed"].size() == 1 &&
                  rel["failed"][0]["model"] == "Qwen3.8-27B-1bit",
              "a model that cannot come back is reported, the others still do, the section is given back");
        CHECK(!fr.exclusive, "the section is given back after a failed reload");
    }
    FakeRouter refusing;
    refusing.refuse_begin = true;
    HeavyWorkHold hold(refusing.hooks());
    json out;
    std::string err;
    CHECK(!hold.begin("x", 60, out, err) && hold.status()["active"] == false,
          "a section that cannot be taken fails the begin and leaves no hold behind");
    CHECK(!hold.begin("x", 0, out, err) && !hold.begin("x", HeavyWorkHold::kMaxSecondsLimit + 1, out, err),
          "max_seconds outside 1..limit is refused");
}

static void test_destructor_does_not_reload() {
    FakeRouter fr;
    fr.loaded["Qwen3-0.6B-1bit"] = {{"options", json::object()}, {"pinned", false}};
    {
        HeavyWorkHold hold(fr.hooks());
        json out;
        std::string err;
        hold.begin("shutdown", 60, out, err);
    }
    CHECK(fr.reloads.empty() && !fr.exclusive, "lemond shutting down ends the hold without starting models again");
}

static void test_router_exclusive_waits_for_owner(lemon::RuntimeConfig& config) {
    lemon::Router router(&config, nullptr, nullptr);
    CHECK(router.begin_exclusive(), "the first owner takes the section");
    std::atomic<bool> second_in{false};
    std::thread second([&] {
        router.begin_exclusive();
        second_in = true;
        router.end_exclusive();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(!second_in.load(), "a second begin_exclusive waits while the first owner holds the section");
    router.end_exclusive();
    second.join();
    CHECK(second_in.load(), "it gets the section once the first owner ends it");
    std::atomic<bool> cancel{false};
    CHECK(router.begin_exclusive(), "the section can be taken again");
    std::thread cancelled([&] {
        cancel = true;
        CHECK(!router.begin_exclusive(&cancel), "a cancelled waiter gives up instead of taking the section");
    });
    cancelled.join();
    router.end_exclusive();
}

int main() {
    json cfg = json::object();
    cfg["max_loaded_models"] = 4;
    cfg["log_level"] = "error";
    lemon::RuntimeConfig config(cfg);
    lemon::RuntimeConfig::set_global(&config);

    test_hold_and_release();
    test_abandoned_wait_is_cancelled();
    test_expiry();
    test_failed_reload_and_refusal();
    test_destructor_does_not_reload();
    test_router_exclusive_waits_for_owner(config);

    lemon::RuntimeConfig::set_global(nullptr);
    std::printf("%s\n", g_failures == 0 ? "heavy_work_hold: ok" : "heavy_work_hold: FAIL");
    return g_failures == 0 ? 0 : 1;
}
