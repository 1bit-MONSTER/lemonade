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

// The onebit backend's slot policy is a property of the variant, not of the recipe: `--onebit npu`
// claims the whole NPU (the array runs one hardware context at a time, so a second NPU model must
// evict the first — issue 0011, engine #1084/#1085) while `--onebit hrx` shares the GPU. The
// router's NPU-exclusivity switch reads this, so getting it wrong either wedges the NPU or evicts
// models that were never in conflict. Contract test, no device and no server needed.

#include "lemon/backends/onebit/onebit.h"
#include "lemon/backends/onebit/onebit_slot_policy.h"

#include <cstdio>
#include <string>

using lemon::SlotPolicy;
using lemon::backends::onebit::descriptor;
using lemon::backends::onebit::slot_policy_for_backend_option;

namespace {

int failures = 0;

void check(const std::string& what, bool ok) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

}  // namespace

int main() {
    check("npu option is exclusive",
          slot_policy_for_backend_option("npu") == SlotPolicy::ExclusiveNpu);
    check("hrx option shares the GPU",
          slot_policy_for_backend_option("hrx") == SlotPolicy::Standard);
    check("auto option does not claim the NPU",
          slot_policy_for_backend_option("auto") == SlotPolicy::Standard);
    check("an unset option does not claim the NPU",
          slot_policy_for_backend_option("") == SlotPolicy::Standard);
    check("an unknown value does not claim the NPU",
          slot_policy_for_backend_option("cuda") == SlotPolicy::Standard);

    // The recipe-level policy stays Standard on purpose: the descriptor is shared by both variants,
    // so claiming the NPU there would evict NPU models on every HRX load.
    check("the descriptor itself stays recipe-level",
          descriptor.slot_policy == SlotPolicy::Standard);
    check("the recipe is the one the router keys on",
          std::string(descriptor.recipe) == "onebit");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ALL ONEBIT SLOT-POLICY TESTS PASSED\n");
    return 0;
}
