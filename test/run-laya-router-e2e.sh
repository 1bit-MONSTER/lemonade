#!/usr/bin/env bash
# Copyright 2026 bong-water-water-bong
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Run the `laya` router e2e (test/server_router.py, test_640) against a lemond started here with a
# stand-in Laya daemon, so it needs no Laya checkpoint, no GPU and no NPU:
#
#   test/run-laya-router-e2e.sh path/to/lemond [port]
#
# It writes the stand-in `1bit route --serve` (the same script the test names), starts lemond with
# LEMONADE_ONEBIT_BIN pointing at it, waits for health, runs that one test with
# LEMONADE_TEST_LAYA_DAEMON set, and tears everything down. PORT defaults to 13505 because 13305 is
# the suite's default and is often already in use on a shared box.
set -uo pipefail

lemond=${1:?usage: run-laya-router-e2e.sh path/to/lemond [port]}
port=${2:-13505}
here=$(cd "$(dirname "$0")" && pwd)
scratch=$(mktemp -d)
trap 'kill -TERM "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; rm -rf "$scratch"' EXIT

# The stand-in daemon: the same protocol as the engine's resident router, keyed on the prompt.
cat >"$scratch/laya-daemon.py" <<'PY'
#!/usr/bin/env python3
import json, sys
print(json.dumps({"ready": True, "load_ms": 1.0}), flush=True)
for line in sys.stdin:
    try:
        state = json.loads(line).get("state", "")
    except ValueError:
        continue
    code = any(k in state for k in ("def ", "function", "class ", "import "))
    print(json.dumps({"class": "code" if code else "prose",
                      "confidence": 0.91 if code else 0.77,
                      "device": "hrx", "decision_ms": 0.4}), flush=True)
PY
chmod +x "$scratch/laya-daemon.py"

export LEMONADE_ONEBIT_BIN="$scratch/laya-daemon.py"
export LEMONADE_TEST_PORT="$port"
export LEMONADE_TEST_LAYA_DAEMON="$scratch/laya-daemon.py"
"$lemond" "$scratch/cache" "$scratch/config" --port "$port" --log-file "$scratch/lemond.log" \
    >"$scratch/lemond.out" 2>&1 &
pid=$!
for _ in $(seq 1 120); do
    curl -sf -m 2 "http://localhost:$port/api/v1/health" >/dev/null && break
    sleep 1
done
curl -sf -m 3 "http://localhost:$port/api/v1/health" >/dev/null || {
    echo "lemond did not come up on :$port"; tail -20 "$scratch/lemond.out"; exit 1; }

# from the test dir, the way the suite itself is invoked (its modules import `utils.*`); the
# interpreter can be overridden, because the suite's deps (openai, psutil, ...) live in a venv on
# this box rather than in the system python.
#   LEMONADE_TEST_PYTHON=~/.cache/lemonade-test-venv/bin/python test/run-laya-router-e2e.sh ...
py=${LEMONADE_TEST_PYTHON:-python3}
cd "$here"
"$py" -m unittest server_router.RouterTests.test_640_laya_classifier_routing -v
rc=$?
echo "--- lemond log (laya lines) ---"
grep -i "laya" "$scratch/lemond.log" | tail -5
exit $rc
