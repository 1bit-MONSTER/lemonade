// Copyright 2026 bong-water-water-bong
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "lemon/backends/onebit/laya_client.h"

#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lemon/backends/onebit/onebit_env.h"
#include "lemon/utils/aixlog.hpp"

namespace lemon {
namespace backends {
namespace onebit {
namespace {

using json = nlohmann::json;

// The scorer's load is the daemon's slow part (2.7 s on a quiet box, 8-10 s under load), and a
// decision is 0.4-0.85 s; both bounds are generous multiples so a busy machine does not turn into
// a failed classifier, while a wedged daemon still cannot hang a request.
constexpr int kStartTimeoutMs = 60000;
constexpr int kDecisionTimeoutMs = 30000;
// After a failed start, do not fork another daemon on every request in a routing storm.
constexpr int kRestartCooldownMs = 5000;

int env_ms(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    const int parsed = std::atoi(value);
    return parsed > 0 ? parsed : fallback;
}

class Daemon {
public:
    ~Daemon() { stop(); }

    // One decision. Starts the daemon if needed; marks it dead on any I/O or protocol failure.
    LayaAnswer decide(const std::string& text) {
        std::lock_guard<std::mutex> lock(mu_);
        LayaAnswer answer;
        if (!ensure_started()) return answer;

        json request{{"state", text}};
        const std::string line = request.dump() + "\n";
        if (!write_all(line)) {
            LOG(WARNING) << "laya: daemon did not accept a request; it will be restarted" << std::endl;
            stop_locked();
            return answer;
        }
        std::string reply;
        const ReadResult read = read_line(reply, decision_timeout_ms_);
        if (read != ReadResult::Ok) {
            if (read == ReadResult::Closed) {
                LOG(WARNING) << "laya: the decision daemon exited; it will be restarted" << std::endl;
            } else {
                LOG(WARNING) << "laya: no decision in " << decision_timeout_ms_
                             << " ms; daemon restarted" << std::endl;
            }
            stop_locked();
            return answer;
        }
        const json parsed = json::parse(reply, nullptr, /*allow_exceptions=*/false);
        if (!parsed.is_object() || !parsed.contains("class") || !parsed["class"].is_string()) {
            LOG(WARNING) << "laya: daemon answered without a class: " << reply.substr(0, 120) << std::endl;
            stop_locked();
            return answer;
        }
        answer.ok = true;
        answer.label = parsed["class"].get<std::string>();
        answer.confidence = parsed.value("confidence", 0.0);
        answer.device = parsed.value("device", std::string{});
        answer.decision_ms = parsed.value("decision_ms", 0.0);
        return answer;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mu_);
        stop_locked();
    }

private:
    bool ensure_started() {
        if (pid_ > 0) return true;
        const auto now = std::chrono::steady_clock::now();
        if (last_failure_.time_since_epoch().count() != 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_failure_).count() <
                kRestartCooldownMs) {
            return false;  // a start failed moments ago; do not fork another on every request
        }

        int to_child[2] = {-1, -1};
        int from_child[2] = {-1, -1};
        if (pipe(to_child) != 0) return fail(now, "pipe");
        if (pipe(from_child) != 0) {
            close(to_child[0]);
            close(to_child[1]);
            return fail(now, "pipe");
        }

        std::vector<std::string> argv = {"route", "--serve"};
        // An explicit checkpoint wins; otherwise the engine resolves it exactly as `serve --laya`
        // does ($ONEBIT_LAYA_MODEL, else ${XDG_DATA_HOME:-~/.local/share}/1bit/laya), so the fork
        // never owns a second copy of that chain.
        if (const char* model = std::getenv("LEMONADE_LAYA_MODEL"); model && *model) {
            argv.push_back("--laya-model");
            argv.push_back(model);
        }
        if (const char* devices = std::getenv("LEMONADE_LAYA_DEVICES"); devices && *devices) {
            argv.push_back("--devices");
            argv.push_back(devices);
        }

        const std::string binary = engine_binary();
        // Everything the child needs is built BEFORE the fork: after fork() in a threaded process only
        // async-signal-safe calls are allowed, and that excludes allocation. lemond is threaded (its
        // thread pool is live while a request routes), so allocating in the child would deadlock on a
        // malloc lock another thread held at fork time - and that deadlock looks exactly like "the
        // daemon never started", in the routing path of a request.
        std::vector<char*> raw;
        raw.reserve(argv.size() + 2);
        raw.push_back(const_cast<char*>(binary.c_str()));
        for (const auto& arg : argv) raw.push_back(const_cast<char*>(arg.c_str()));
        raw.push_back(nullptr);
        char* const* argv_raw = raw.data();

        const pid_t pid = fork();
        if (pid < 0) return fail(now, "fork");
        if (pid == 0) {
            dup2(to_child[0], STDIN_FILENO);
            dup2(from_child[1], STDOUT_FILENO);
            close(to_child[0]);
            close(to_child[1]);
            close(from_child[0]);
            close(from_child[1]);
            // Do not hand the daemon lemond's descriptors: they would keep its listening sockets open
            // (a restarted lemond then finds the port taken) and outlive it. Only 0/1/2 are kept.
            for (int fd = 3; fd < 256; ++fd) close(fd);
            execvp(binary.c_str(), argv_raw);
            _exit(127);  // exec failed: the parent sees EOF and reports a failed classifier
        }
        close(to_child[0]);
        close(from_child[1]);
        write_fd_ = to_child[1];
        read_fd_ = from_child[0];
        pid_ = pid;

        // The daemon announces itself once the scorer is loaded: without that line a client cannot
        // tell a slow load from a slow decision.
        std::string ready;
        if (read_line(ready, start_timeout_ms_) != ReadResult::Ok) {
            LOG(WARNING) << "laya: daemon did not report ready in " << start_timeout_ms_
                         << " ms (is the Laya checkpoint installed? docs/laya.md)" << std::endl;
            stop_locked();
            return fail(now, nullptr);
        }
        const json parsed = json::parse(ready, nullptr, /*allow_exceptions=*/false);
        if (!parsed.is_object() || parsed.value("ready", false) != true) {
            LOG(WARNING) << "laya: daemon did not say ready: " << ready.substr(0, 120) << std::endl;
            stop_locked();
            return fail(now, nullptr);
        }
        LOG(INFO) << "laya: decision daemon ready in " << static_cast<long>(parsed.value("load_ms", 0.0))
                  << " ms (" << binary << ")" << std::endl;
        return true;
    }

    bool fail(std::chrono::steady_clock::time_point now, const char* what) {
        last_failure_ = now;
        if (what) LOG(WARNING) << "laya: cannot start the decision daemon: " << what << ": " << std::strerror(errno) << std::endl;
        return false;
    }

    void stop_locked() {
        if (write_fd_ >= 0) {
            close(write_fd_);
            write_fd_ = -1;
        }
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            for (int i = 0; i < 50; ++i) {  // up to 5 s of grace, then SIGKILL
                int status = 0;
                if (waitpid(pid_, &status, WNOHANG) == pid_) {
                    pid_ = -1;
                    break;
                }
                usleep(100 * 1000);
            }
            if (pid_ > 0) {
                kill(pid_, SIGKILL);
                waitpid(pid_, nullptr, 0);
                pid_ = -1;
            }
        }
        if (read_fd_ >= 0) {
            close(read_fd_);
            read_fd_ = -1;
        }
    }

    bool write_all(const std::string& data) {
        std::size_t written = 0;
        while (written < data.size()) {
            const ssize_t n = write(write_fd_, data.data() + written, data.size() - written);
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            written += static_cast<std::size_t>(n);
        }
        return true;
    }

    enum class ReadResult { Ok, Timeout, Closed };

    // One line from the daemon's stdout, bounded by `timeout_ms`. The daemon writes whole lines
    // with a flush per decision, so a short read means a partial line is still owed.
    ReadResult read_line(std::string& out, int timeout_ms) {
        if (!pending_.empty()) {  // a previous read returned two lines
            const auto newline = pending_.find('\n');
            if (newline != std::string::npos) {
                out = pending_.substr(0, newline);
                pending_.erase(0, newline + 1);
                return ReadResult::Ok;
            }
            out = pending_;
            pending_.clear();
            return ReadResult::Ok;
        }
        out.clear();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (true) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();
            if (left <= 0) return ReadResult::Timeout;
            struct pollfd pfd = {read_fd_, POLLIN, 0};
            const int ready = poll(&pfd, 1, static_cast<int>(left));
            if (ready < 0) {
                if (errno == EINTR) continue;
                return ReadResult::Closed;
            }
            if (ready == 0) return ReadResult::Timeout;
            char chunk[4096];
            const ssize_t n = read(read_fd_, chunk, sizeof(chunk));
            if (n < 0) {
                if (errno == EINTR) continue;
                return ReadResult::Closed;
            }
            if (n == 0) return ReadResult::Closed;  // the daemon exited
            out.append(chunk, static_cast<std::size_t>(n));  // may hold more than one line
            const auto newline = out.find('\n');
            if (newline != std::string::npos) {
                pending_ = out.substr(newline + 1);
                out.resize(newline);
                return ReadResult::Ok;
            }
        }
    }

    std::mutex mu_;
    pid_t pid_ = -1;
    int write_fd_ = -1;
    int read_fd_ = -1;
    std::string pending_;
    std::chrono::steady_clock::time_point last_failure_{};
    // Read from the environment so a test (and a slow or overloaded box) can adjust them without a
    // rebuild; the defaults are the shipped bounds.
    const int start_timeout_ms_ = env_ms("LEMONADE_LAYA_START_TIMEOUT_MS", kStartTimeoutMs);
    const int decision_timeout_ms_ = env_ms("LEMONADE_LAYA_DECISION_TIMEOUT_MS", kDecisionTimeoutMs);
};

Daemon& daemon() {
    static Daemon instance;
    return instance;
}

}  // namespace

LayaAnswer laya_decide(const std::string& text) { return daemon().decide(text); }

void laya_shutdown() { daemon().stop(); }

}  // namespace onebit
}  // namespace backends
}  // namespace lemon
