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

// The Laya decision daemon: one `1bit route --serve` process, kept warm for the life of
// lemond. The engine's resident router answers one decision per stdin line
// (docs/laya-first.md §8 step 1), which is what makes a per-request Laya decision affordable:
// the scorer's multi-second load is paid once at daemon start instead of per request.
#pragma once

#include <string>

namespace lemon {
namespace backends {
namespace onebit {

// One answer from the daemon. `ok` false means "no decision" (daemon absent, dead, slow or
// babbling) — the caller reports a failed classifier, never a wrong route.
struct LayaAnswer {
    bool ok = false;
    std::string label;        // code | prose | short | long_doc
    double confidence = 0.0;  // 0..1, Laya's calibrated confidence
    std::string device;       // what the engine's own route policy picked (informational)
    double decision_ms = 0.0;
};

// Ask the resident daemon what request class `text` is. Starts the daemon on first use and
// restarts it if it died. Never throws and never blocks longer than the configured timeouts: a
// routing decision must not be able to hang a request.
LayaAnswer laya_decide(const std::string& text);

// Stop the daemon (SIGTERM, then SIGKILL after a grace period). Idempotent; tests use it to
// exercise restart, and process shutdown calls it through a static destructor.
void laya_shutdown();

}  // namespace onebit
}  // namespace backends
}  // namespace lemon
