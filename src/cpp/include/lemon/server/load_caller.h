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

#include <cstdio>
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
//
// One line, always: a body or User-Agent that carries a newline would otherwise write extra lines, and
// the last of them can read as any other log record. That is not an attack scenario -- `curl -d @load.json`
// with a pretty-printed body does it by accident, and the `-A` header is the client's to set. So control
// characters are escaped, and so are the quote and the backslash, which otherwise make the escapes
// themselves ambiguous.
inline std::string load_field(const std::string& s, size_t max_chars) {
    std::string escaped;
    escaped.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\x%02x", c);
                    escaped += buf;
                } else {
                    escaped += static_cast<char>(c);
                }
        }
    }
    if (escaped.size() <= max_chars) return escaped;
    return escaped.substr(0, max_chars) + "\u2026(" + std::to_string(s.size()) + " bytes)";
}

inline std::string load_caller(const httplib::Request& http) {
    std::ostringstream out;
    out << "from " << (http.remote_addr.empty() ? std::string("?") : http.remote_addr)
        << " ua=\"" << load_field(http.get_header_value("User-Agent"), 120) << "\""
        << " body=" << load_field(http.body, 512);
    return out.str();
}

}  // namespace lemon
