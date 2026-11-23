#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace lemon {

// Frees every model slot while work outside lemond needs the GPU, the NPU or the RAM (a build, a quantization, a
// benchmark, a second large model), and brings the same models back with the same options afterwards.
//
// A hold runs on its own thread for its whole life, because the router's exclusive section is owned by a thread: it
// takes the section (in-flight turns finish first), captures and evicts every loaded model, and keeps the section
// until it is released or `max_seconds` passes. Requests that arrive meanwhile wait in the router's load path instead
// of loading a model, and run once the captured models are back. At most one hold is active.
class HeavyWorkHold {
public:
    using json = nlohmann::json;

    struct Hooks {
        // takes the router's exclusive section; returns false when *cancel becomes true before it is taken (the
        // caller of begin() gave up), and then nothing is evicted
        std::function<bool(std::atomic<bool>* cancel)> begin_exclusive;
        std::function<void()> end_exclusive;
        // model name -> {"options": {...}, "pinned": bool}, for every model it evicted
        std::function<std::map<std::string, json>()> evict_all;
        // reloads one captured model; throws on failure
        std::function<void(const std::string& model, const json& captured)> reload;
        std::function<std::chrono::steady_clock::time_point()> now = [] { return std::chrono::steady_clock::now(); };
    };

    explicit HeavyWorkHold(Hooks hooks);
    ~HeavyWorkHold();

    HeavyWorkHold(const HeavyWorkHold&) = delete;
    HeavyWorkHold& operator=(const HeavyWorkHold&) = delete;

    // Blocks until the models are out. On success fills {"hold_id", "reason", "max_seconds", "unloaded"}; returns
    // false with `error` when a hold is already active or the exclusive section could not be taken.
    // wait_seconds bounds how long begin() waits for the section (in-flight requests, a running job). On that deadline
    // the attempt is cancelled and nothing is unloaded: a hold must never start after its caller has given up, or every
    // model would stay out for max_seconds with no one left to release it.
    bool begin(const std::string& reason, int max_seconds, json& out, std::string& error,
               int wait_seconds = kDefaultWaitSeconds);

    // Blocks until the captured models are reloaded. Fills {"hold_id", "reloaded", "failed", "expired"}; false when
    // `hold_id` is not the active hold.
    bool release(const std::string& hold_id, json& out);

    // {"active": false} or {"active": true, "hold_id", "reason", "since_s", "max_seconds", "phase", "unloaded"}
    json status() const;

    static constexpr int kMaxSecondsLimit = 24 * 3600;
    static constexpr int kDefaultWaitSeconds = 300;   // below 1bit-heavy's 600 s client timeout
    static constexpr int kMaxWaitSeconds = 3600;

private:
    enum class Phase { Idle, Starting, Holding, Releasing };

    void run(std::string id, int max_seconds);
    void join_finished_locked(std::unique_lock<std::mutex>& lock);

    Hooks hooks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    Phase phase_ = Phase::Idle;
    bool release_requested_ = false;
    bool shutting_down_ = false;
    bool start_failed_ = false;
    std::atomic<bool> cancel_start_{false};   // set when begin() gives up waiting for the section
    bool expired_ = false;
    std::string id_;
    std::string reason_;
    int max_seconds_ = 0;
    std::chrono::steady_clock::time_point since_{};
    std::vector<std::string> unloaded_;
    json last_result_ = json::object();
    unsigned long counter_ = 0;
};

} // namespace lemon
