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

// Where the onebit engine binary is. One rule, two callers: the backend's
// `resolve_binary()` and the Laya client, which runs `1bit route --serve` as a
// resident decision daemon. $LEMONADE_ONEBIT_BIN, else `1bit` from $PATH.
#pragma once

#include <cstdlib>
#include <string>

namespace lemon {
namespace backends {
namespace onebit {

inline std::string engine_binary() {
    if (const char* env = std::getenv("LEMONADE_ONEBIT_BIN"); env && *env) return env;
    return "1bit";
}

}  // namespace onebit
}  // namespace backends
}  // namespace lemon
