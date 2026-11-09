<!--
Copyright 2026 bong-water-water-bong
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# The onebit catalog, entry by entry

`src/cpp/resources/server_models.json` carries 27 `onebit` entries: **22 proven on the HRX route** (the rows
below, one per catalog entry), **1 added and not proven yet** (flint-40b-a2.5b — it loads and then its
logits are all NaN, so the row has one named blocker in the engine: #1539, see the note below), and 4 NPU
model directories (a separate route, added in #20). This page records how each HRX
entry was proven on Strix Halo, so a reader can tell an entry that runs from one that was only added.

## What "proven" means here

One private `lemond` (own cache and config dirs, a free port, `--no-broadcast`; the HF cache is symlinked
at the files already on the box, so nothing is downloaded), every GPU job inside a `gpu-hold` claim, and per
entry:

1. `lemonade pull NAME` resolves the checkpoint,
2. `lemonade load NAME --onebit hrx` — strict placement is the engine's default, so a graph node the
   kernels cannot run **fails the load**. A refusal is the test, not a warning, and there is no
   `--no-strict-device` anywhere in this proof,
3. one request in the entry's own role (chat, embeddings or reranking),
4. for chat entries, `lemonade bench NAME --backend hrx --scenarios chat-short --runs 3 --warmup 1` —
   Lemonade's standard CI protocol — which is where the tok/s and TTFT below come from.

A hold with any `amdgpu` page fault, ring timeout or GPU reset in `dmesg` would invalidate its entries.
There were none: **0 faults in all 16 holds** across both runs (9 in run 1, 7 in run 2, including the three
re-measured rows).

## Entries and their proofs

| model | load (strict placement, no off-HRX node) | request | lemonade bench standard / chat-short (3 runs, warmup 1) | proven on |
|---|---|---|---|---|
| MiniCPM5-1B-1bit | 1.5 s | 200: <reasoning only> First, the question is: "What is the capital of Franc | **262.5 tok/s** (min 261.0, max 264.0), TTFT 22 ms, 30/20 tok, failed 0/3 | 22 Oct, engine 4b645cb |
| MiniCPM-V-4.6-1bit | 2.1 s | 200: The capital | **206.6 tok/s** (min 205.9, max 207.0), TTFT 50 ms, 29/20 tok, failed 0/3 | 23 Oct, engine f37f51c |
| LFM2-1.2B-1bit | 1.9 s | 200: The capital of France is Paris. | **233.8 tok/s** (min 233.3, max 234.7), TTFT 47 ms, 28/20 tok, failed 0/3 | 22 Oct, engine 4b645cb |
| Qwen3-0.6B-1bit | 1.4 s | 200: <reasoning only> Okay, the user is asking for the capital of France in | **330.7 tok/s** (min 329.6, max 332.8), TTFT 20 ms, 27/20 tok, failed 0/3 | 22 Oct, engine 4b645cb |
| Qwen3-1.7B-1bit | 1.8 s | 200: <reasoning only> Okay, the user is asking for the capital of France. I | **166.0 tok/s** (min 165.7, max 166.4), TTFT 28 ms, 27/20 tok, failed 0/3 | 22 Oct, engine 4b645cb |
| Qwen3-4B-1bit | 2.9 s | 200: <reasoning only> Okay, the user is asking for the capital of France. L | **79.4 tok/s** (min 79.2, max 79.5), TTFT 55 ms, 27/20 tok, failed 0/3 | 22 Oct, engine 4b645cb |
| Qwen3-8B-1bit | 4.1 s | 200: <reasoning only> Okay, the user is asking for the capital of France. L | **43.4 tok/s** (min 43.2, max 43.7), TTFT 88 ms, 27/20 tok, failed 0/3 | 23 Oct, engine f37f51c |
| Qwen3.5-4B-1bit | 4.2 s | 200: <reasoning only> Thinking Process: 1. **Analyze the Request:** * Quest | **61.2 tok/s** (min 61.0, max 61.3), TTFT 155 ms, 29/20 tok, failed 0/3 | 22 Oct, engine 4b645cb |
| Qwen3-Embedding-0.6B-1bit | 1.8 s | 200: 2 vectors, dim 1024 | n/a | 23 Oct, engine f37f51c |
| bge-reranker-v2-m3-1bit | 1.5 s | 200: ranked 0:7.782,2:-1.256,1:-5.362,3:-11.018 | n/a | 23 Oct, engine f37f51c |
| Qwen3-Reranker-0.6B-1bit | 1.9 s | 200: ranked 2:0.986,0:0.014,1:0.000,3:0.000 | n/a | 23 Oct, engine f37f51c |
| GLM-4.7-Flash-1bit | 15.6 s | 200: <reasoning only> 1. **Analyze the Request:** * Question: "What is the  | **63.0 tok/s** (min 62.9, max 63.0), TTFT 170 ms, 20/20 tok, failed 0/3 | 23 Oct, engine f37f51c |
| Qwen3-4B-Instruct-2507-1bit | 3.6 s | 200: The capital of France is Paris. | **78.3 tok/s** (min 75.9, max 79.8), TTFT 56 ms, 27/20 tok, failed 0/3 | 22 Oct, engine 4b645cb |
| nomic-embed-text-v2-moe-1bit | 1.4 s | 200: 2 vectors, dim 768 | n/a | 23 Oct, engine f37f51c |
| jina-reranker-v1-tiny-en-1bit | 1.2 s | 200: ranked 0:0.130,1:0.035,2:0.030,3:0.010 | n/a | 23 Oct, engine f37f51c |
| ZAYA1-8B-1bit | 3.0 s | 200: <reasoning only> Okay, the user is asking for the capital of France an | **91.5 tok/s** (min 91.2, max 91.9), TTFT 300 ms, 30/20 tok, failed 0/3 | 23 Oct, engine f37f51c |
| ZAYA1-8B-PM-1bit | 4.8 s | 200: Paris. | n/a | 23 Oct, engine f37f51c |
| Qwen2.5-Coder-7B-Instruct-1bit | 5.4 s | 200: The capital of France is Paris. | **47.0 tok/s** (min 47.0, max 47.1), TTFT 91 ms, 27/20 tok, failed 0/3 | 23 Oct, engine f37f51c |
| Qwen2.5-Coder-1.5B-Instruct-1bit | 2.5 s | 200: Paris is the capital of France. | **165.7 tok/s** (min 164.7, max 166.3), TTFT 31 ms, 27/20 tok, failed 0/3 | 22 Oct, engine 4b645cb |
| Qwen3-Coder-30B-A3B-Instruct-1bit | 12.4 s | 200: The capital of France is Paris. | **84.6 tok/s** (min 83.6, max 85.2), TTFT 102 ms, 27/20 tok, failed 0/3 | 23 Oct, engine f37f51c |
| Qwen3.8-27B-1bit | 12.3 s | 200: Paris is the capital of France. | **26.1 tok/s** (min 26.0, max 26.1), TTFT 807 ms, 67/20 tok, failed 0/3 | 23 Oct, engine f37f51c |
| gpt-oss-20b-mxfp4-1bit | 5.7 s | 200: Paris is | **72.3 tok/s** (min 65.8, max 76.6), TTFT 238 ms, 88/20 tok, failed 0/3 | 22 Oct, engine 4b645cb |
| flint-40b-a2.5b | **200 in 21.2 s** (strict placement) | *500: all-NaN logits* | *pending* | **loads and then fails in the sampler** — engine #1539 (see the note below) |

## Notes on individual rows

- **flint-40b-a2.5b** is the newest entry and the one row above that is **not proven yet**.
  `unsloth/flint-GGUF:flint-UD-Q4_K_XL.gguf` (25.05 GB, 39.8 B total / ~2.5 B active, 256 routed experts
  top-6 plus a shared expert, 49 layers, hybrid 35 sliding-window / 14 full attention) is a new
  architecture, and the row's earlier reason for being pending — that no engine build carried the `flint`
  arch — **is no longer true**: the arch landed in our llama.cpp fork on 2026-10-23 (`c3eef46c0`, "models:
  add Flint (FlintForCausalLM), ported from upstream #30928") and is present at every llama.cpp sha the
  shipping engine bundles report.

  **Then memory, and now neither.** On 2026-11-08 01:01 (engine `rc-2026-11-05-c25`, the standard protocol
  above, one `gpu-hold` claim) the engine's own planner refused the load in 223 ms:

  ```
  1bit serve: flint-UD-Q4_K_XL does not fit: need 24.07 GiB (flint-UD-Q4_K_XL.gguf: weights 23.33 + KV/state
  0.24 + compute 0.50) at the model's own ctx 262144, free 6.03 GiB (GTT 29.1 of 120.0 GiB free,
  MemAvailable 6.0 GiB); GTT after this load would reach 115.0 GiB of the 104.0 GiB ceiling ...
  ```

  What that line does and does not show. It does **not** show the architecture is understood: the admission
  check is architecture-generic (`app/mem_admit.h`, `model_need()`), reading `context_length`, `block_count`,
  `attention.head_count_kv` and `attention.sliding_window` out of any GGUF header without ever asking
  llama.cpp whether it can build the arch — an unknown arch gets a sized KV and the same 223 ms "does not fit".
  The arch being supported rests on the fork evidence above, not on this line. What the line does show is that
  the refusal is **transient**: four foreign HRX clients were resident and the box was at load 46.6, leaving
  6.0 GiB of 122 GiB, against flint's **24.07 GiB** minimum (weights alone 23.33 GiB).

  **And 24.07 GiB is a lower bound, so the re-run condition has to pin the context.** `model_need()` takes the
  share of layers that attend over the *full* context from a per-arch list (`gpt-oss`, `gemma2` and `gemma3` at
  one in 2, 5 or 6 layers) or from an `attention.sliding_window_pattern` array in the file; "an architecture
  not listed counts every layer as windowed (the estimate stays a lower bound)", `flint` is not on that list, so
  unless its GGUF carries an `attention.sliding_window_pattern` array every one of its 49 layers is priced at the
  window. flint's 14 full-attention layers are then not priced at
  the model's own 262144 context, which is why the same 24.07 GiB comes back at ctx 4096, 8192 and 262144 alike.
  Priced properly those 14 layers add roughly `0.24 GiB × 14/49 × 262144/(window+512)` — about 3.9 GiB for a
  4096-token window — so on a box with 24-28 GiB free the planner can **admit** flint at its own context and the
  load then overruns the GTT ceiling (the engine #680 shape the refusal line cites). The honest re-run is
  therefore **`--ctx-size 8192` with >= 24 GiB free**, where the windowed estimate and the real need are close;
  a run at the model's own context needs its own figure, not this one.

  That refusal was the box, not flint: the next attempt, on a quiet box (**MemAvailable 66 GiB**, load 6.16, tctl
  73 °C), **loaded the model in 21.2 s with strict placement** and got as far as decoding — and then every one of
  the 150 144 logits came back **NaN**, so llama.cpp's HRX guardrail aborted the server before it could sample:

  ```
  common/sampling.cpp:620: HRX: NaN logits (engine#123)
  E common_sampler_sample: HRX returned NaN logits: count=150144 of 150144, ... contiguous=yes, continuing=no
  ```

  `llama-server` exits **134** and the request answers 500; the router's retries aborted identically (4/4), so it is
  deterministic, not a race.

  **The trigger is serve's own warm-up, measured (2026-11-09).** Two arms in one `gpu-hold`, same lemond, same checkpoint,
  68 GiB free, one **short** client chat ("say hello") at ctx 8192: with the warm-ups that chat answers 500 and the log carries
  6 NaN lines; with **`ONEBIT_NO_WARMUP=1`** the same load answers **200 with text** and the log carries **0**. The hold was
  clean around both — 0 amdgpu fault lines, 0 leftover backends — and the abort is on the first *decode*, not the load, which
  rules out placement and the graph.

  What that does and does not license. It puts the failure in the **request serve sends before it says ready**, not in a
  client's own traffic — a distinction that matters because the warm-ups are two different requests: the first samples
  (`/v1/chat/completions`, a ~485-token prompt, `max_tokens 8`), the second does not (`/completion`, `" a"` × 16,
  `n_predict 0`). **One short chat cannot separate "the warm-up poisons the graph" from "a prompt of the warm-up's length
  fails wherever it comes from"**, and the number that makes that live is `sliding_window 513` against that ~485-token
  prompt. If it is the length, then `ONEBIT_NO_WARMUP=1` moves the failure to the first client request of that length rather
  than removing it. The arm that decides it is engine **#1689**'s `ONEBIT_WARMUP=long|short`. Until it runs, the honest
  statement is the narrow one: with the warm-ups off, a *short* chat answers 200 with text and 0 NaN lines; whether a client
  prompt as long as the warm-up's also fails is unmeasured.

  So the row's cells stay *pending* on engine **[#1539](https://github.com/1bit-MONSTER/engine/issues/1539)**, and the reason
  recorded here is the request, not the architecture: the arch is in the pin and the load is accepted, and what is not yet
  known is which warm-up-induced condition the sampler chokes on.

  **Sampling** the model recommends, and what the GGUF's own `general.sampling.*` metadata already
  carries: **temp 1.0, top-p 0.95, top-k 20**. It is a reasoning model — it emits `<think> … </think>`
  before any answer — so a request that caps output around 64 tokens shows reasoning only; give it an
  **output budget of ~32K tokens** (the model's context is 262,144) when the answer matters. On the
  host-side CPU check the same three prompts answered coherently at temp 0 greedy with a ceiling of
  400-800 tokens.

- **Qwen3.8-27B** carries its own MTP head (`blk.64.nextn.*`), so the onebit engine drafts with it by
  default: the numbers are the **MTP-default** configuration, not a bare decode.
- **MiniCPM-V-4.6** is proven **text-only**: the onebit backend passes no `mmproj`, so the entry has no
  `vision` label and the request is a plain chat completion.
- **ZAYA1-8B-PM** runs the engine's Project Manager route (`onebit_pm`), which answers or delegates each
  chat to another model through this Lemonade; its request is a 993-token prompt answered "Paris." — it is
  not a chat-throughput configuration, so it has no bench row.
- **The embedding and reranking entries** answer 200 with the expected shape (2 vectors at 1024 and 768
  dimensions; reranker scores ordered as expected); Lemonade's `chat-short` bench does not apply to them.
- **Three rows (Qwen3-8B, Qwen2.5-Coder-7B, ZAYA1-8B) were re-measured.** Their first pass ran beside
  another agent's compile, which the fleet's queue now serialises: the same models measured 45.2 / 48.5 /
  96.0 tok/s in the 22 Oct run and their 23 Oct re-run is what this table carries.

## Not in the catalog yet

| model | why |
|---|---|
| Qwen3.6-35B-A3B, Gemma-4-26B-A4B-it, Gemma-4-31B-it, Qwen3.6-27B-MTP, Muse-Glimmer-30B, Nemotron-3.5-Lightning-30B-A3B, Qwen3-Coder-Next, gpt-oss-120b | Lemonade's hot models never proven on onebit: a download of 19-63 GB would break the box's free-disk floor. Not a known HRX refusal. |
| Gemma-4 MTP variants (separate draft file) | a onebit entry cannot carry a draft checkpoint; the engine drafts only from a model's own MTP head. |
| vision through onebit | the backend passes no `mmproj`; vision models are catalogued text-only. |
| NPU model directories (Qwen3-0.6B/1.7B/4B-NPU, Qwen3.6-35B-A3B-NPU) | the NPU route, added in #20 and proven there; out of scope for this page. |

## How this was run

Both runs on Strix Halo (Ryzen AI MAX+ 395, Radeon 8060S, gfx1151, 128 GB), kernel
`7.3.0-rc4-1bit-perfopt`, private `lemond` from this fork, `1bit serve` one hop, one model at a time.

| run | Lemonade | engine | labels | when |
|---|---|---|---|---|
| 1 | fork `main` `e9dd9a2f` | `4b645cb` (llama.cpp `af04c9f2`, hrx-system `82804197`, gpu-kernels `cf222e7d`) | `repaste 1 (paste)` | 22 Oct, 9 holds |
| 2 | fork `main` `b0eb4f2d` (rebuilt for this run) | `f37f51c` (gpu-kernels `4f586cac`) | `90 W cap, PCM pad (23 Oct)` | 23 Oct, 7 holds |

The two runs overlap on eight entries and agree within a few percent (GLM-4.7-Flash 63.0 / 63.0 tok/s,
Qwen3.8-27B 26.0 / 26.1, Qwen3-Coder-30B-A3B 85.4 / 84.6, MiniCPM-V-4.6 207.1 / 206.6), so the engine
move did not change the catalog's numbers. Power labels are the owner's: the box was repasted on 22 Oct
(paste) and again on 23 Oct 01:38 (phase-change pad); a number is only comparable with another carrying
the same label.

Raw evidence: the runner's per-entry state (`state-run1-4b645cb.json`, `state-run2.json`), per-entry
Lemonade logs and the bench JSON, on the proof machine.
