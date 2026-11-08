// Copyright 2026 bong-water-water-bong
// SPDX-License-Identifier: Apache-2.0
//
// The unit of `size` in server_models.json: ONE definition, so a reader cannot
// disagree with the writer again (engine#1381, found in lemonade#43's review).
//
// The field is **decimal gigabytes** (1e9 bytes). The writer is
// tools/model_sizes.py, which sums the HF repo's blob bytes and divides by 1e9
// and says so in its own docstring; all 293 rows have followed that convention
// since lemonade#43.
//
// Two readers assumed 2^30 instead, so every tool-written row disagreed with
// what those readers reported by 7.4 % (2^30 / 1e9 = 1.0737): the Ollama
// /api/tags route reported a 10.0 GB model as 10 737 418 240 bytes, and
// artifact_store rounded by 1024^3. Both now convert through this header, and
// router.cpp's "is this model too big for the NPU driver" threshold names its
// unit in bytes rather than a bare 10.0.
//
// Why decimal and not binary: the number is a *display* of "how big is this
// model", computed from bytes, and HF (its source) reports bytes. A binary
// gigabyte would be equally defensible as a unit - but then the writer changes,
// ~293 rows are rewritten, and every consumer of the field has to move at once.
// Fixing two readers to the convention the data already has is the smaller and
// safer correction, and it is the one that leaves the field's meaning written
// down in exactly one place.
#pragma once

#include <cstdint>

namespace lemon {
namespace units {

// Bytes in one gigabyte of the `size` field: decimal, not binary.
inline constexpr double kBytesPerGigabyte = 1e9;

// The ratio a 2^30 reader used to apply, kept named so a test can assert the
// readers do *not* apply it (10.0 in the field is 1e10 bytes, never 1.0737e10).
inline constexpr double kBytesPerGibibyte = 1073741824.0;

// `size` (decimal GB) -> bytes, as an integer count. Saturates rather than
// overflowing: the negative guard is for a caller that round-trips a computed
// value through a signed type.
inline int64_t gigabyte_to_bytes(double gigabytes) {
    if (!(gigabytes > 0.0)) {
        return 0;
    }
    return static_cast<int64_t>(gigabytes * kBytesPerGigabyte);
}

// bytes -> `size` (decimal GB) in the field's own unit, unrounded: callers that
// display it choose their own precision, as artifact_store does.
inline double bytes_to_gigabyte(uint64_t bytes) {
    return static_cast<double>(bytes) / kBytesPerGigabyte;
}

// router.cpp's NPU guard: a model larger than this, in the field's unit, is
// refused when the auto-context budget fell back. Stated in bytes and derived,
// so the constant cannot drift from the unit.
inline constexpr double kNpuModelTooLargeBytes = 10.0 * kBytesPerGigabyte;
inline constexpr double kNpuModelTooLargeGigabytes = kNpuModelTooLargeBytes / kBytesPerGigabyte;

}  // namespace units
}  // namespace lemon
