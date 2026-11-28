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

// onebit::mmproj_args (engine#3549): a onebit model with an mmproj checkpoint is served with it, except an
// NPU model directory, which `1bit serve` refuses a projector for.

#include "lemon/backends/onebit/onebit_vision.h"

#include <cstdio>
#include <string>
#include <vector>

using lemon::backends::onebit::mmproj_args;
using lemon::backends::onebit::mmproj_env;

static int g_failures = 0;

static void check(const char* name, bool ok) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_failures;
}

int main() {
    const std::string proj = "/models/snap/mmproj-F16.gguf";
    check("a GGUF model with its projector on disk gets --mmproj <path>",
          mmproj_args(proj, false, true) == std::vector<std::string>{"--mmproj", proj});
    check("a model without an mmproj checkpoint gets no flag", mmproj_args("", false, false).empty());
    check("a projector that is registered but not on disk gets no flag (the model loads for text)",
          mmproj_args(proj, false, false).empty());
    check("an NPU model directory gets no projector (1bit serve refuses one there)", mmproj_args(proj, true, true).empty());
    check("an NPU model directory without a projector gets none either", mmproj_args("", true, false).empty());
    // engine#3549/#3588: a model served with a projector keeps it on the CPU (the HRX image path aborts the server)
    const auto env = mmproj_env(mmproj_args(proj, false, true));
    check("a projector served gets LLAMA_ARG_MMPROJ_OFFLOAD=false (projector on the CPU)",
          env.size() == 1 && env[0].first == "LLAMA_ARG_MMPROJ_OFFLOAD" && env[0].second == "false");
    check("no projector, no environment change", mmproj_env(mmproj_args("", false, false)).empty());
    check("an NPU pack (no projector passed) gets no environment change", mmproj_env(mmproj_args(proj, true, true)).empty());
    if (g_failures) {
        std::printf("FAILED: %d\n", g_failures);
        return 1;
    }
    std::printf("All onebit vision checks passed\n");
    return 0;
}
