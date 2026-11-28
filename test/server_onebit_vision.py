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

"""Images through the onebit recipe on HRX (engine#3549, #3588): answered, with the backend never restarted.

The payload's HRX llama-server aborts when the vision projector runs on HRX under strict placement (clip_encode,
IM2COL off HRX, exit 134), which lemond answers by restarting the backend. onebit therefore keeps the projector on the
CPU (LLAMA_ARG_MMPROJ_OFFLOAD=false, onebit_vision.h). This suite registers a small vision model on onebit with an
mmproj checkpoint and checks, for onebit_backend hrx and auto:
  - an image request (OpenAI image_url and Ollama /api/generate images) answers 200 with text naming the colour;
  - the backend process serving the model is the same before and after (no restart; pid from /api/v1/health);
  - a text request after the images still answers on that same process.

Needs a box with the 1bit engine (LEMONADE_ONEBIT_BIN) and network access for the model. Run against a running lemond:
    LEMONADE_TEST_PORT=<port> python server_onebit_vision.py
"""

import base64
import struct
import zlib

import requests

from utils.server_base import ServerTestBase, run_server_tests
from utils.test_models import PORT

TIMEOUT = 600
MAIN = "unsloth/Qwen3.5-0.8B-GGUF:Qwen3.5-0.8B-UD-Q4_K_XL.gguf"
MMPROJ = "unsloth/Qwen3.5-0.8B-GGUF:mmproj-F16.gguf"
BASE = f"http://localhost:{PORT}"


def red_png(size=16):
    """A solid red PNG, built here so the suite carries no binary fixture."""
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + b"\xff\x00\x00" * size for _ in range(size))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


IMG = base64.b64encode(red_png()).decode()
QUESTION = "What colour is this image? Answer with one word."


def backend_pid(model):
    health = requests.get(f"{BASE}/api/v1/health", timeout=30).json()
    for m in health.get("all_models_loaded", []):
        if m.get("model_name") == model:
            return m.get("pid")
    return None


class OnebitVisionTests(ServerTestBase):
    def run_device(self, device):
        model = f"user.onebit-vision-test-{device}"
        r = requests.post(f"{BASE}/api/v1/pull", timeout=TIMEOUT, json={
            "model_name": model, "recipe": "onebit", "labels": ["vision"],
            "checkpoints": {"main": MAIN, "mmproj": MMPROJ}, "recipe_options": {"onebit_backend": device}})
        self.assertEqual(r.status_code, 200, r.text[:300])
        r = requests.post(f"{BASE}/api/v1/load", timeout=TIMEOUT, json={"model_name": model})
        self.assertEqual(r.status_code, 200, r.text[:300])
        pid = backend_pid(model)
        self.assertIsNotNone(pid, "the loaded model reports no backend pid in /api/v1/health")

        r = requests.post(f"{BASE}/api/v1/chat/completions", timeout=TIMEOUT, json={
            "model": model, "max_tokens": 24, "messages": [{"role": "user", "content": [
                {"type": "text", "text": QUESTION + " /no_think"},
                {"type": "image_url", "image_url": {"url": "data:image/png;base64," + IMG}}]}]})
        self.assertEqual(r.status_code, 200, f"image_url on {device}: {r.text[:300]}")
        msg = r.json()["choices"][0]["message"]
        answer = (msg.get("content") or "") + " " + (msg.get("reasoning_content") or "")
        self.assertIn("red", answer.lower(), f"image_url on {device} did not name the colour: {answer[:200]!r}")

        r = requests.post(f"{BASE}/api/generate", timeout=TIMEOUT, json={
            "model": model, "prompt": QUESTION, "images": [IMG], "stream": False, "think": False,
            "options": {"num_predict": 24}})
        self.assertEqual(r.status_code, 200, f"/api/generate images on {device}: {r.text[:300]}")
        self.assertIn("red", r.json().get("response", "").lower(), f"/api/generate on {device}: {r.text[:200]}")

        self.assertEqual(backend_pid(model), pid, f"the backend restarted during the image requests on {device}")
        r = requests.post(f"{BASE}/api/v1/chat/completions", timeout=TIMEOUT, json={
            "model": model, "max_tokens": 16, "messages": [{"role": "user", "content": "What is 2+3? /no_think"}]})
        self.assertEqual(r.status_code, 200, r.text[:300])
        self.assertEqual(backend_pid(model), pid, f"the backend restarted after the image requests on {device}")
        requests.post(f"{BASE}/api/v1/unload", timeout=60, json={"model_name": model})
        print(f"[OK] {device}: images answered (red), text after them answered, backend pid {pid} unchanged")

    def test_001_hrx(self):
        self.run_device("hrx")

    def test_002_auto(self):
        self.run_device("auto")


if __name__ == "__main__":
    run_server_tests(OnebitVisionTests, "ONEBIT VISION (images on HRX, projector on the CPU, no backend restart)")
