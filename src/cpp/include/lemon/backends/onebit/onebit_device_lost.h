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

// The engine's device-lost answer on the NPU route, as it reaches Lemonade.
//
// A token's work is one runlist of layer runs. When the NPU runtime reports that a run did not
// complete, `1bit unified` answers the request that hit it with this frame and then exits non-zero
// (engine docs/npu.md, "When a dispatch fails"), because the context's own state is dead: measured on
// the c22 board, the same process answered 614 to every later request while a fresh one answered
// normally. Exiting hands the problem to the supervisor -- Lemonade's backend watchdog -- which alone
// can build a new context.
//
// Lemonade relays that 500 to the client, so a user loses ONE request. A non-streamed request has
// received no bytes from the client's point of view, so replaying it once on the context the reload
// builds is safe and turns that loss into a slow answer. See Router::execute_inference.
//
// The engine's own classifier is `npu_run_failed()` in app/serve_watchdog.h; this is the same
// predicate over the text Lemonade sees, so the two cannot drift into disagreeing about which
// failures mean "the context is gone". Narrow on purpose: the runtime's own prefix plus code 614 (or
// its ERT state name), never a bare "state 8", which other failures also carry.
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace lemon {
namespace backends {
namespace onebit {

// True for the runtime's failed-runlist message: "npu runtime: invalid call (code 614): state 8,
// run 0 of 28", or the same failure named by its ERT state (ERT_CMD_STATE_TIMEOUT).
inline bool is_npu_run_failure(const std::string& message) {
    if (message.find("npu runtime:") == std::string::npos) return false;
    return message.find("(code 614)") != std::string::npos ||
           message.find("ERT_CMD_STATE_TIMEOUT") != std::string::npos;
}

// The texts a backend error carries: the wrapper's own `message`, the backend's body under
// `details.response`, and the Response body itself (when the caller passes one unwrapped).
inline bool device_lost_in(const nlohmann::json& value) {
    if (value.is_string()) return is_npu_run_failure(value.get<std::string>());
    if (!value.is_object()) return false;
    for (const char* key : {"message", "error"}) {
        auto it = value.find(key);
        if (it != value.end() && device_lost_in(*it)) return true;
    }
    auto details = value.find("details");
    if (details != value.end() && details->is_object()) {
        auto response = details->find("response");
        if (response != details->end() && device_lost_in(*response)) return true;
    }
    return false;
}

// True when this response is the engine saying an NPU run did not complete, so the backend's context
// is dead and the answer is worth exactly one replay after the backend has been reloaded.
inline bool is_device_lost_response(const nlohmann::json& response) { return device_lost_in(response); }

}  // namespace onebit
}  // namespace backends
}  // namespace lemon
