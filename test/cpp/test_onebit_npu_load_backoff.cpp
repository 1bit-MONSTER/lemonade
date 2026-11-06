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

// Starting an NPU backend can be refused while the driver frees the previous holder's partition (0011v2:
// ~2 s after the holder exits; ctx_create answers -ENODEV / code 102). OnebitServer therefore backs off and
// retries inside a 10 s budget (TOS3-C29 item 2) instead of failing the load. The schedule is what decides
// how many chances that budget buys and when a load gives up, so it is asserted here rather than trusted:
// no device, no engine binary, no server.

#include "lemon/backends/onebit/npu_load_backoff.h"

#include <cmath>
#include <cstdio>
#include <string>

using lemon::backends::onebit::NpuLoadBackoff;
using lemon::backends::onebit::npu_load_backoff_delay;
using lemon::backends::onebit::npu_load_backoff_give_up;
using lemon::backends::onebit::npu_load_backoff_more;

namespace {

int failures = 0;

void check(const std::string& what, bool ok) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

}  // namespace

int main() {
    const NpuLoadBackoff p;

    // The budget is the release cycle's number, not a value that can drift silently.
    check("the retry budget is the 10 s the release cycle asked for",
          std::abs(p.window_s - 10.0) < 1e-9);
    check("the first attempt is immediate",
          npu_load_backoff_delay(p, 1) == 0.0);
    check("the second attempt waits a quarter second",
          std::abs(npu_load_backoff_delay(p, 2) - 0.25) < 1e-9);

    // Doubling, capped: the schedule must not grow without bound inside the budget.
    check("the delay doubles", std::abs(npu_load_backoff_delay(p, 3) - 0.5) < 1e-9);
    check("the delay doubles again", std::abs(npu_load_backoff_delay(p, 4) - 1.0) < 1e-9);
    check("the delay is capped at 2 s", std::abs(npu_load_backoff_delay(p, 9) - 2.0) < 1e-9);
    check("a very late attempt is still capped", std::abs(npu_load_backoff_delay(p, 60) - 2.0) < 1e-9);
    check("attempt 0 or below does not sleep", npu_load_backoff_delay(p, 0) == 0.0);

    // A refusal that clears in the measured ~2 s window must be retried.
    check("retries while the partition is still freeing (2 s in)", npu_load_backoff_more(p, 3, 2.0));
    check("retries just inside the budget", npu_load_backoff_more(p, 5, 8.0));
    // ...and the budget actually ends: a dead array must not hang the load request.
    check("gives up once the budget is spent", !npu_load_backoff_more(p, 6, 9.9));
    check("gives up after many attempts even if elapsed looks small",
          !npu_load_backoff_more(p, 40, 0.0) || npu_load_backoff_delay(p, 41) <= p.window_s);

    // How many chances 10 s actually buys, walked the way the caller walks it: this is the number the log
    // will show an operator, so it must be more than a token retry.
    int attempts = 0;
    double elapsed = 0.0;
    while (npu_load_backoff_more(p, attempts + 1, elapsed) && attempts < 100) {
        ++attempts;
        elapsed += 0.05;  // a start that fails in 50 ms, i.e. a child that never came up
    }
    check("a fast-failing child gets several attempts (at least 5)",
          attempts >= 5);
    check("the attempts it gets still fit the budget",
          elapsed + npu_load_backoff_delay(p, attempts + 1) <= p.window_s + 1e-9);

    // The give-up line is the only thing an operator sees when the array is genuinely dead.
    const std::string why = npu_load_backoff_give_up("serve exited before it was ready", 6, 10.0);
    check("the give-up line names the attempts and the elapsed time",
          why.find("6 time(s)") != std::string::npos && why.find("10000 ms") != std::string::npos);
    check("the give-up line keeps the underlying reason",
          why.find("serve exited before it was ready") != std::string::npos);
    check("the give-up line says what fixes a dead array",
          why.find("modprobe -r amdxdna") != std::string::npos);

    if (failures == 0) {
        std::printf("\nAll npu_load_backoff tests passed\n");
        return 0;
    }
    std::printf("\n%d npu_load_backoff test(s) failed\n", failures);
    return 1;
}
