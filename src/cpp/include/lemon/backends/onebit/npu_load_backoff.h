/*
 * Copyright 2026 bong-water-water-bong
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <string>

namespace lemon {
namespace backends {
namespace onebit {

// Starting an NPU backend is a request for the array, not a fork: the engine opens an AMDXDNA hardware
// context, and the driver refuses one for ~2 s after the previous holder exits (0011v2 frees the
// partition late; the refusal is `-ENODEV` / code 102 at ctx_create). Lemonade evicts the old NPU
// holder before it starts the new one, so on a model switch the replacement child lands exactly in
// that window and the load fails for a reason that is gone a moment later.
//
// The engine queues a refusal internally (`1bit serve --load-queue-ms`); this is the same patience held
// on the side that decides to start the child at all, so a payload whose engine predates that queue,
// or a refusal the engine reports instead of absorbing, still ends in a load instead of a 500.
//
// What this side CANNOT do is tell a refusal from a broken load. `WrappedServer::start_server()` throws
// a fixed "<backend> failed to start" -- the child's own reason ("code 102", "No such device", a missing
// kernel directory) is printed to the log and never reaches the exception -- so a caller here has no text
// to classify, and this PR retries every start failure rather than pretending to filter. The costs that
// buys are named at the loop in onebit_server.cpp; making a text predicate possible means carrying the
// child's last lines out of ServerProcess, which is its own change.
struct NpuLoadBackoff {
    // The c29 board evidence (TOS3-C29 item 2): the release check reads code 102 for ~2 s after the
    // holder exits. 10 s is the budget the release cycle asked for — long enough for the partition,
    // short enough that a genuinely dead array still answers the load request promptly.
    double window_s = 10.0;
    double first_delay_s = 0.25;
    double max_delay_s = 2.0;
};

// The delay before the next attempt. Attempt 1 is the immediate first try and never sleeps.
inline double npu_load_backoff_delay(const NpuLoadBackoff& p, int attempt) {
    if (attempt <= 1) return 0.0;
    double d = p.first_delay_s;
    for (int i = 2; i < attempt; ++i) d = std::min(d * 2.0, p.max_delay_s);
    return std::min(d, p.max_delay_s);
}

// Whether another attempt fits in the budget after `attempts` failures that took `elapsed_s` so far.
// It bounds the DELAYS: an attempt is itself bounded by start_server's own ready-wait (600 s), so a load
// whose child takes seconds to fail can run past this window. The give-up line says "of delays" for that
// reason, and a reader should not read `window_s` as a hard bound on the load.
inline bool npu_load_backoff_more(const NpuLoadBackoff& p, int attempts, double elapsed_s) {
    return elapsed_s + npu_load_backoff_delay(p, attempts + 1) <= p.window_s;
}

// What the operator reads when the budget ran out: name the refusal, the wait, and what fixes it.
inline std::string npu_load_backoff_give_up(const std::string& detail, int attempts, double elapsed_s) {
    return "the NPU did not start the backend in " + std::to_string(attempts) + " attempt(s) over " +
           std::to_string(int(elapsed_s * 1000)) + " ms of delays (last: " + detail +
           "); another client may still be releasing the array, or the driver needs a reload "
           "(modprobe -r amdxdna && modprobe amdxdna)";
}

}  // namespace onebit
}  // namespace backends
}  // namespace lemon
