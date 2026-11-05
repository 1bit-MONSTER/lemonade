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

// The Laya decision daemon client (src/cpp/server/backends/onebit/laya_client.cpp): it keeps one
// `1bit route --serve` process warm and answers a decision per line. A fake daemon stands in for
// the engine here (LEMONADE_ONEBIT_BIN points at a script), so this runs with no model, no Laya
// checkpoint and no device. What it pins:
//   - a decision reaches the caller with class, confidence, device and decision_ms,
//   - the ready line is required: a daemon that never says ready is a failed classifier, and the
//     start is not retried on every request (cooldown),
//   - a daemon that dies mid-life costs one failed decision and is restarted on the next call,
//   - a daemon that answers garbage is a failed decision, not a wrong label,
//   - a binary that does not exist is a failed decision, reported without blocking,
//   - LEMONADE_LAYA_MODEL / LEMONADE_LAYA_DEVICES reach the daemon's argv.
//
// usage: test_laya_client            (CTest runs it from the build dir)

#include "lemon/backends/onebit/laya_client.h"

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using lemon::backends::onebit::laya_decide;
using lemon::backends::onebit::laya_shutdown;

static int g_failures = 0;

static void check(const char* name, bool ok) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_failures;
}

namespace {

// A fake `1bit route --serve` in bash: records its argv, says ready, then answers every {"state"}
// line by echoing the state back as the class, so the test can see what reached it.
const char* kFakeDaemon = R"SH(#!/usr/bin/env bash
printf '%s\n' "$@" > "$LAYA_TEST_ARGV"
echo '{"ready":true,"load_ms":12.5}'
while IFS= read -r line; do
    case "$line" in
        *'"state":"die"'*)   exit 0 ;;                       # the daemon dies mid-life
        *'"state":"garbage"'*) echo 'not json at all' ;;      # a babbling daemon
        *'"state":"slow"'*)  sleep 40 ; echo '{"class":"code","confidence":0.5}' ;;
        *) echo '{"class":"code","confidence":0.91,"device":"hrx","decision_ms":412.7}' ;;
    esac
done
)SH";

// A daemon that starts but never loads the scorer (no ready line).
const char* kSilentDaemon = R"SH(#!/usr/bin/env bash
sleep 400
)SH";

std::string write_script(const fs::path& dir, const char* name, const char* body) {
    const fs::path path = dir / name;
    std::ofstream(path) << body;
    fs::permissions(path, fs::perms::owner_all);
    return path.string();
}

}  // namespace

int main() {
    const fs::path scratch = fs::temp_directory_path() / ("laya-client-" + std::to_string(::getpid()));
    fs::create_directories(scratch);
    // The shipped bounds are 60 s / 30 s; the failure cases below are about *reaching* the bound, not
    // about waiting on it, so shrink them (the client reads both from the environment).
    ::setenv("LEMONADE_LAYA_START_TIMEOUT_MS", "1500", 1);
    ::setenv("LEMONADE_LAYA_DECISION_TIMEOUT_MS", "1500", 1);
    const std::string argv_log = (scratch / "argv.txt").string();
    ::setenv("LAYA_TEST_ARGV", argv_log.c_str(), 1);

    const std::string fake = write_script(scratch, "fake-1bit", kFakeDaemon);
    ::setenv("LEMONADE_ONEBIT_BIN", fake.c_str(), 1);
    ::unsetenv("LEMONADE_LAYA_MODEL");
    ::unsetenv("LEMONADE_LAYA_DEVICES");

    // 1. A decision, and it carries everything the classifier needs.
    auto answer = laya_decide("Write a Python function that reverses a linked list.");
    check("a decision reaches the caller", answer.ok && answer.label == "code");
    check("the decision carries Laya's confidence", answer.ok && answer.confidence > 0.9 && answer.confidence < 0.92);
    check("the decision carries the daemon's device and cost",
          answer.device == "hrx" && answer.decision_ms > 400.0);
    check("the daemon wrote its argv", fs::exists(argv_log));
    if (fs::exists(argv_log)) {
        std::ifstream in(argv_log);
        std::string first(static_cast<std::size_t>(fs::file_size(argv_log)), '\0');
        in.read(&first[0], static_cast<std::streamsize>(first.size()));
        check("the daemon is started as `route --serve`", first.find("route") == 0 && first.find("--serve") != std::string::npos);
    }

    // 2. A second decision reuses the same daemon (no second start, no argv rewrite).
    const fs::file_time_type first_stamp = fs::last_write_time(argv_log);
    answer = laya_decide("Explain how transformers work.");
    check("a second decision reuses the warm daemon", answer.ok && fs::last_write_time(argv_log) == first_stamp);

    // 3. A daemon that answers garbage costs one failed decision, and the next call still works.
    auto garbage = laya_decide("garbage");
    check("a babbling daemon is a failed decision, not a wrong label", !garbage.ok);
    auto after_garbage = laya_decide("Write a Rust function that sums a slice.");
    check("the client recovers after a babbling daemon", after_garbage.ok && after_garbage.label == "code");

    // 4. A daemon that dies mid-life: one failed decision, then a restart on the next call.
    auto died = laya_decide("die");
    check("a daemon that exits costs one failed decision", !died.ok);
    ::usleep(200 * 1000);
    auto restarted = laya_decide("Write a Go function that reverses a string.");
    check("the next call restarts the daemon", restarted.ok && restarted.label == "code");

    laya_shutdown();

    // 5. A binary that does not exist: failed decision, and it is not retried on every request.
    const std::string missing = (scratch / "no-such-1bit").string();
    ::setenv("LEMONADE_ONEBIT_BIN", missing.c_str(), 1);
    const auto t0 = std::chrono::steady_clock::now();
    check("a missing engine binary is a failed decision", !laya_decide("anything").ok);
    const auto first_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - t0)
                              .count();
    const auto t1 = std::chrono::steady_clock::now();
    check("the failure is not retried inside the cooldown", !laya_decide("anything").ok);
    const auto second_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - t1)
                               .count();
    check("a failed start does not block a request", first_ms < 5000 && second_ms < 1000);

    // 6. A daemon that never becomes ready is a failed decision (the scorer did not load).
    ::usleep(5200 * 1000);  // past the restart cooldown a failed start leaves behind
    ::setenv("LEMONADE_ONEBIT_BIN", write_script(scratch, "silent-1bit", kSilentDaemon).c_str(), 1);
    check("a daemon that never says ready is a failed decision", !laya_decide("anything").ok);
    laya_shutdown();

    // 7. The checkpoint and device overrides reach the daemon's argv.
    ::usleep(5200 * 1000);  // past the cooldown the silent daemon left behind
    ::setenv("LEMONADE_ONEBIT_BIN", fake.c_str(), 1);
    ::setenv("LEMONADE_LAYA_MODEL", "/tmp/some/checkpoint", 1);
    ::setenv("LEMONADE_LAYA_DEVICES", "hrx,npu", 1);
    fs::remove(argv_log);
    check("the overrides are honoured", laya_decide("anything").ok);
    check("the second daemon wrote its argv", fs::exists(argv_log));
    if (fs::exists(argv_log)) {
        std::ifstream in(argv_log);
        std::string args(static_cast<std::size_t>(fs::file_size(argv_log)), '\0');
        in.read(&args[0], static_cast<std::streamsize>(args.size()));
        check("LEMONADE_LAYA_MODEL reaches --laya-model", args.find("--laya-model") != std::string::npos &&
                                                            args.find("/tmp/some/checkpoint") != std::string::npos);
        check("LEMONADE_LAYA_DEVICES reaches --devices", args.find("--devices") != std::string::npos &&
                                                           args.find("hrx,npu") != std::string::npos);
    }
    laya_shutdown();
    fs::remove_all(scratch);

    std::printf("\n%s\n", g_failures == 0 ? "ALL LAYA CLIENT TESTS PASSED" : "LAYA CLIENT TESTS FAILED");
    return g_failures == 0 ? 0 : 1;
}
