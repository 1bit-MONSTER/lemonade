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

// Which backend answers mean "the NPU's context is dead, the request is worth one replay".
//
// Router::execute_inference replays a non-streamed request exactly once when the backend says a run
// did not complete, because the engine answers that frame and exits. The predicate is narrow on
// purpose: a false positive replays a request on a reloaded context for nothing (slow), and a false
// negative is the 500 the c29 take lost a turn to. Every body below is either the engine's own text
// (npu.md, "When a dispatch fails") or a neighbouring failure that must NOT replay.
#include <lemon/backends/onebit/onebit_device_lost.h>

#include <cstdio>
#include <string>

using lemon::backends::onebit::is_device_lost_response;
using lemon::backends::onebit::is_npu_run_failure;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const std::string& name) {
    ++g_checks;
    if (condition) {
        std::printf("[PASS] %s\n", name.c_str());
    } else {
        std::printf("[FAIL] %s\n", name.c_str());
        ++g_failures;
    }
}

static bool lost(const std::string& body) {
    return is_device_lost_response(nlohmann::json::parse(body));
}

int main() {
    // ---- the message itself -------------------------------------------------------------
    check(is_npu_run_failure("npu runtime: invalid call (code 614): state 8, run 0 of 28"),
          "the runtime's failed-runlist message is a device loss");
    check(is_npu_run_failure("npu runtime: invalid call (code 614): ERT_CMD_STATE_TIMEOUT, run 3 of 28"),
          "the same failure named by its ERT state is a device loss");
    check(!is_npu_run_failure("state 8, run 0 of 28"),
          "a bare 'state 8' is not one (other failures carry it)");
    check(!is_npu_run_failure("npu runtime: invalid call (code 102): No such device"),
          "code 102 (the driver refused a start) is not a lost context");
    check(!is_npu_run_failure("npu runtime: invalid call (code 615): state 8, run 0 of 28"),
          "another 6xx code is not the run-failed contract");
    check(!is_npu_run_failure("Compute error"),
          "an HRX compute error stays the watchdog's (is_watchdog_reset_response)");

    // ---- the response Lemonade produced in the c29 take ---------------------------------
    // lemond-onebit.log, 2026-11-05 22:17:41.292, verbatim (whitespace only). This is the turn the
    // picture page lost, and the one a replay is for.
    const std::string c29 =
        R"J({"details":{"backend":"llama-server","response":{"error":{"message":"npu runtime: invalid call (code 614): state 8, run 0 of 28"}}},"message":"npu runtime: invalid call (code 614): state 8, run 0 of 28","status_code":500,"type":"backend_error"})J";
    check(lost(c29), "the c29 take's 500 body is a device loss");

    check(lost(R"J({"error":{"message":"npu runtime: invalid call (code 614): state 8, run 0 of 28"}})J"),
          "the same answer wrapped under 'error' is a device loss");

    check(lost(R"J({"error":{"message":"npu runtime: ERT_CMD_STATE_TIMEOUT at run 3 of 28"}})J"),
          "the ERT-named failure is a device loss too");

    // The text only under details.response: the shape a caller passing the backend's own body in
    // would see. Still the engine's failure, so still a device loss.
    check(lost(R"J({"error":{"details":{"backend":"llama-server","response":{"error":{"message":"npu runtime: invalid call (code 614): state 8, run 0 of 28"}}}}})J"),
          "the engine's frame under details.response counts too");

    // ---- must not replay ----------------------------------------------------------------
    check(!lost(R"J({"error":{"message":"npu runtime: invalid call (code 102): No such device"}})J"),
          "a refused start is not replayed here (the load path has its own backoff)");
    check(!lost(R"J({"error":{"message":"Model 'x' is not loaded","type":"model_not_loaded"}})J"),
          "an evicted model is not a device loss");
    check(!lost(R"J({"error":{"message":"Compute error","type":"backend_error"}})J"),
          "an HRX compute error is not one (watchdog reset handles it)");
    check(!lost(R"J({"choices":[{"text":"ok"}]})J"), "a normal completion is not a device loss");
    check(!lost("{}"), "an empty object is not a device loss");
    check(!is_device_lost_response(nlohmann::json("a plain string body")),
          "an unparseable/plain body is not a device loss");
    check(!lost(R"J({"error":{"details":{"response":"npu runtime: invalid call (code 615)"}}})J"),
          "a neighbouring code inside details is not a device loss");
    check(!lost(R"J({"error":{"message":"state 8, run 0 of 28"}})J"),
          "a bare 'state 8' inside a response is not a device loss");

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("All onebit device-lost checks passed.\n");
        return 0;
    }
    std::printf("%d onebit device-lost check(s) failed.\n", g_failures);
    return 1;
}
