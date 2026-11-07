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

// A load that fails is anonymous in the log unless the caller is named: the c28 take has three
// POST /api/v1/load requests carrying onebit_backend=hrx that no shipped script admits to sending, and
// lemond could not say who sent them. This asserts the line that answers it, including the two ways it
// could lie — dropping the client, or clipping a body silently.

#include "lemon/server/load_caller.h"

#include <cstdio>
#include <string>

#include <httplib.h>

namespace {

int failures = 0;

void check(const std::string& what, bool ok) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    {
        httplib::Request r;
        r.remote_addr = "127.0.0.1";
        r.set_header("User-Agent", "onehop_bench/1.0");
        r.body = R"({"model_name":"Qwen3-0.6B-NPU-1bit","onebit_backend":"hrx","ctx_size":4096})";
        const std::string line = lemon::load_caller(r);
        check("the peer address is named", has(line, "from 127.0.0.1"));
        check("the User-Agent is named", has(line, "ua=\"onehop_bench/1.0\""));
        check("the body is named", has(line, "onebit_backend") && has(line, "ctx_size"));
        check("a short body is not clipped", !has(line, "bytes)"));
    }

    {
        // httplib leaves remote_addr empty for some transports; "?" beats an empty field that reads
        // like a bug in the caller.
        httplib::Request r;
        r.body = "{}";
        const std::string line = lemon::load_caller(r);
        check("a missing peer address prints as ?", has(line, "from ?"));
        check("a missing User-Agent prints as empty", has(line, "ua=\"\""));
    }

    {
        // The bound, and the marker that keeps it honest.
        httplib::Request r;
        r.remote_addr = "10.0.0.9";
        r.body = std::string(2000, 'x');
        const std::string line = lemon::load_caller(r);
        check("a long body is clipped", line.size() < 900);
        check("the clip says how long the real body was", has(line, "(2000 bytes)"));
        check("and it stays one line", line.find('\n') == std::string::npos);
    }

    {
        httplib::Request r;
        r.remote_addr = "10.0.0.9";
        r.set_header("User-Agent", std::string(500, 'a'));
        const std::string line = lemon::load_caller(r);
        check("a long User-Agent is clipped", line.size() < 900);
    }

    {
        // The review's forgery case, verbatim in shape: a body that is pretty-printed carries newlines,
        // and the last line of one can read as any other record. `curl -d @load.json` does this with no
        // ill intent at all, so the line has to survive it.
        httplib::Request r;
        r.remote_addr = "127.0.0.1";
        r.set_header("User-Agent", "curl/8");
        r.body = "{\n  \"model_name\": \"x\"\n}\n[Info] (Server) Model loaded: FORGED by the request body";
        const std::string line = lemon::load_caller(r);
        check("a multi-line body stays one line", line.find('\n') == std::string::npos &&
                                                       line.find('\r') == std::string::npos);
        check("its newlines are escaped, not dropped", has(line, "\\n  \\\"model_name\\\""));
        check("so the forged record is inside the body field, not a line of its own",
              has(line, "body={ ") || has(line, "body={\\n"));
    }

    {
        // A User-Agent is the client's to set, including a quote that would close its field and let the
        // rest read as separate fields (`ua="x" body=...`). httplib refuses a header value containing a
        // raw newline at parse time, so a quote is the part of this that can actually reach load_caller.
        httplib::Request r;
        r.body = "{}";
        r.set_header("User-Agent", "x\" body=forged");
        const std::string line = lemon::load_caller(r);
        check("a quote in the User-Agent cannot close its field", has(line, "ua=\"x\\\" body=forged\" body={}"));
    }

    {
        // Backslashes are escaped too, so an escape in the output is never mistaken for a raw newline
        // that was written by the request.
        httplib::Request r;
        r.body = "a\\nb";  // four characters: a, backslash, n, b
        const std::string line = lemon::load_caller(r);
        check("a literal backslash-n in the body is not a newline escape", has(line, "body=a\\\\nb"));
    }

    if (failures == 0) {
        std::printf("\nAll load_caller tests passed\n");
        return 0;
    }
    std::printf("\n%d load_caller test(s) failed\n", failures);
    return 1;
}
