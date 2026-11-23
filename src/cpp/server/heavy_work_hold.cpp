#include "lemon/heavy_work_hold.h"

#include <utility>

#include "lemon/utils/aixlog.hpp"

namespace lemon {

HeavyWorkHold::HeavyWorkHold(Hooks hooks) : hooks_(std::move(hooks)) {}

HeavyWorkHold::~HeavyWorkHold() {
    std::unique_lock<std::mutex> lock(mutex_);
    // lemond is going down: end the hold, but do not start models that would be stopped again at once.
    shutting_down_ = true;
    release_requested_ = true;
    cv_.notify_all();
    lock.unlock();
    if (worker_.joinable()) worker_.join();
}

void HeavyWorkHold::join_finished_locked(std::unique_lock<std::mutex>& lock) {
    if (phase_ != Phase::Idle || !worker_.joinable()) return;
    std::thread done = std::move(worker_);
    lock.unlock();
    done.join();
    lock.lock();
}

bool HeavyWorkHold::begin(const std::string& reason, int max_seconds, json& out, std::string& error,
                          int wait_seconds) {
    if (max_seconds <= 0 || max_seconds > kMaxSecondsLimit) {
        error = "max_seconds must be between 1 and " + std::to_string(kMaxSecondsLimit);
        return false;
    }
    if (wait_seconds <= 0 || wait_seconds > kMaxWaitSeconds) {
        error = "wait_seconds must be between 1 and " + std::to_string(kMaxWaitSeconds);
        return false;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    join_finished_locked(lock);
    if (phase_ != Phase::Idle) {
        error = "a hold is already active (" + id_ + ": " + reason_ + ")";
        return false;
    }
    id_ = "hold-" + std::to_string(++counter_);
    reason_ = reason;
    max_seconds_ = max_seconds;
    phase_ = Phase::Starting;
    release_requested_ = false;
    start_failed_ = false;
    expired_ = false;
    unloaded_.clear();
    last_result_ = json::object();
    cancel_start_ = false;
    worker_ = std::thread(&HeavyWorkHold::run, this, id_, max_seconds);
    bool gave_up = false;
    if (!cv_.wait_for(lock, std::chrono::seconds(wait_seconds), [&] { return phase_ != Phase::Starting; })) {
        // the deadline: cancel the attempt and wait for the worker to come back. It either returns without evicting
        // anything, or it took the section in the same instant - then the hold stands, and this caller still gets it
        gave_up = true;
        cancel_start_ = true;
        cv_.wait(lock, [&] { return phase_ != Phase::Starting; });
    }
    if (start_failed_) {
        join_finished_locked(lock);
        error = gave_up ? "the model slots could not be taken within " + std::to_string(wait_seconds) +
                              " s (requests in flight or a job hold them); nothing was unloaded"
                        : "the model slots could not be taken";
        return false;
    }
    out = {{"hold_id", id_}, {"reason", reason_}, {"max_seconds", max_seconds_}, {"unloaded", unloaded_}};
    return true;
}

bool HeavyWorkHold::release(const std::string& hold_id, json& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (phase_ == Phase::Idle || hold_id != id_) {
        join_finished_locked(lock);
        // A hold that expired on its own still answers its own release, so a wrapper that outlived it is told.
        if (hold_id == id_ && last_result_.contains("hold_id")) {
            out = last_result_;
            return true;
        }
        return false;
    }
    release_requested_ = true;
    cv_.notify_all();
    cv_.wait(lock, [&] { return phase_ == Phase::Idle; });
    out = last_result_;
    join_finished_locked(lock);
    return true;
}

HeavyWorkHold::json HeavyWorkHold::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (phase_ == Phase::Idle) return {{"active", false}};
    const char* phase = phase_ == Phase::Starting ? "starting" : phase_ == Phase::Holding ? "holding" : "releasing";
    const double since = std::chrono::duration<double>(hooks_.now() - since_).count();
    return {{"active", true},     {"hold_id", id_},        {"reason", reason_},     {"since_s", since},
            {"max_seconds", max_seconds_}, {"phase", phase}, {"unloaded", unloaded_}};
}

void HeavyWorkHold::run(std::string id, int max_seconds) {
    if (!hooks_.begin_exclusive(&cancel_start_)) {
        std::lock_guard<std::mutex> lock(mutex_);
        start_failed_ = true;
        phase_ = Phase::Idle;
        cv_.notify_all();
        return;
    }
    std::map<std::string, json> captured;
    try {
        captured = hooks_.evict_all();
    } catch (const std::exception& e) {
        LOG(WARNING, "Hold") << "evicting the models for " << id << " failed: " << e.what() << std::endl;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    for (const auto& kv : captured) unloaded_.push_back(kv.first);
    since_ = hooks_.now();
    const auto deadline = since_ + std::chrono::seconds(max_seconds);
    phase_ = Phase::Holding;
    LOG(INFO, "Hold") << id << " (" << reason_ << "): " << unloaded_.size()
                      << " model(s) unloaded, requests wait until it is released" << std::endl;
    cv_.notify_all();
    while (!release_requested_) {
        if (cv_.wait_until(lock, deadline) == std::cv_status::timeout && hooks_.now() >= deadline) {
            expired_ = true;
            LOG(WARNING, "Hold") << id << " reached max_seconds=" << max_seconds << " without a release; releasing it"
                                 << std::endl;
            break;
        }
    }
    phase_ = Phase::Releasing;
    const bool reload = !shutting_down_;
    lock.unlock();

    json reloaded = json::array();
    json failed = json::array();
    for (const auto& kv : captured) {
        if (!reload) break;
        try {
            hooks_.reload(kv.first, kv.second);
            reloaded.push_back(kv.first);
        } catch (const std::exception& e) {
            LOG(WARNING, "Hold") << id << ": could not bring " << kv.first << " back: " << e.what() << std::endl;
            failed.push_back({{"model", kv.first}, {"error", e.what()}});
        }
    }
    hooks_.end_exclusive();

    lock.lock();
    last_result_ = {{"hold_id", id}, {"reloaded", reloaded}, {"failed", failed}, {"expired", expired_}};
    phase_ = Phase::Idle;
    cv_.notify_all();
}

} // namespace lemon
