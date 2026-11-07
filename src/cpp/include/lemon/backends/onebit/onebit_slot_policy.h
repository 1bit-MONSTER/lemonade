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
#include "lemon/backends/backend_descriptor.h"

#include <string>

namespace lemon {
namespace backends {
namespace onebit {

// NPU exclusivity is a property of the VARIANT, not of the recipe, so the descriptor's single
// `slot_policy` cannot express it:
//   `--onebit npu`  the engine owns the whole NPU. The array runs one hardware context at a time:
//                   a second live context from another client wedges it (issue 0011, engine
//                   #1084/#1085), so loading a second NPU model must evict the first.
//   `--onebit hrx`  the model runs on the GPU through our HIP kernels and shares that device like
//                   any other GPU backend, so it must not evict NPU models.
//   `auto` / empty  the engine chooses the device, so this side cannot claim the NPU. The engine
//                   refuses a second NPU context with code 102 and the caller queues; the driver
//                   fix (per-client exclusivity) is what makes that safe.
inline SlotPolicy slot_policy_for_backend_option(const std::string& onebit_backend) {
    return onebit_backend == "npu" ? SlotPolicy::ExclusiveNpu : SlotPolicy::Standard;
}

// The same question for a load that has a checkpoint in hand: an NPU model directory runs on the NPU
// whatever `--onebit` says (the engine refuses the pairing otherwise), so it claims the NPU too. Without
// this, a load asked for with `--onebit hrx` on an .q4nx pack would start a second NPU context beside a
// resident one — the wedge issue 0011 is about — and then fail anyway.
inline SlotPolicy slot_policy_for_model(const std::string& onebit_backend, bool npu_model_path) {
    if (npu_model_path) return SlotPolicy::ExclusiveNpu;
    return slot_policy_for_backend_option(onebit_backend);
}

}  // namespace onebit
}  // namespace backends
}  // namespace lemon
