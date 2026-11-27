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

// The vision projector a onebit model is served with (engine#3549).
//
// A model registered with an `mmproj` checkpoint (a vision or audio projector) can only take image parts when
// llama-server is started with it. `1bit serve --mmproj <file>` hands it to llama-server on the llama.cpp devices
// (hrx, cpu) and refuses it for an NPU model directory ("--mmproj works on the llama.cpp devices"), so an NPU
// pack gets none: passing it there would turn a working text load into a refused one. Without this, OnebitServer
// never read the checkpoint, and an `images` request reached a llama-server started with no projector (HTTP 500
// "image input is not supported"). It is the part of LlamaCppServer's rule ("Add mmproj file if present") that applies to
// onebit: its `hf_load` skip does not (OnebitServer always serves the resolved checkpoint, never `-hf`), and its CPU
// `--no-mmproj-offload` is `1bit serve`'s to add for --device cpu (it builds that llama-server argv; onebit cannot pass the
// flag). A projector that is registered but not on disk yet gets no flag, so the model still loads for text instead of
// the whole load failing on `--mmproj <missing>` (fork #66 review).

#pragma once

#include <string>
#include <vector>

namespace lemon {
namespace backends {
namespace onebit {

// The `1bit serve` arguments for a model's projector: {"--mmproj", path}, or none when it has no projector, the
// projector file is not on disk (`on_disk` false), or the checkpoint is an NPU model directory.
inline std::vector<std::string> mmproj_args(const std::string& mmproj_path, bool npu_pack, bool on_disk) {
    if (mmproj_path.empty() || npu_pack || !on_disk) return {};
    return {"--mmproj", mmproj_path};
}

}  // namespace onebit
}  // namespace backends
}  // namespace lemon
