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

#include <sstream>
#include <string>

#include <httplib.h>

namespace lemon {

// Who asked for a load. A load that fails is otherwise anonymous in the log: the c28 take has three
// `POST /api/v1/load` requests carrying `onebit_backend=hrx` that no shipped script admits to sending
// (the release check, puppy-check and the image all lack the string), and nothing in lemond could name
// the caller. The access log records the path, not the body or the client, so this names all three:
// the peer address, the User-Agent, and the request body.
//
// Bounded on purpose: a load body is a model name plus options, and a stray megabyte must not become a
// log line. The clip marker carries the true length so a truncated body is never mistaken for a short one.
inline std::string load_caller(const httplib::Request& http) {
    auto clip = [](const std::string& s, size_t n) {
        if (s.size() <= n) return s;
        return s.substr(0, n) + "…(" + std::to_string(s.size()) + " bytes)";
    };
    std::ostringstream out;
    out << "from " << (http.remote_addr.empty() ? std::string("?") : http.remote_addr)
        << " ua=\"" << clip(http.get_header_value("User-Agent"), 120) << "\""
        << " body=" << clip(http.body, 512);
    return out.str();
}

}  // namespace lemon
