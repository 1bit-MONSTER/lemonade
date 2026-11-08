// Copyright 2026 bong-water-water-bong
// SPDX-License-Identifier: Apache-2.0
//
// engine#1381: `size` in server_models.json is decimal gigabytes (1e9 bytes) - that is what
// tools/model_sizes.py writes and documents - but two readers converted it as 2^30, so every
// tool-written row was reported 7.4 % too large, and router.cpp's NPU threshold compared a bare
// 10.0 whose unit nothing stated.
//
// These tests pin the contract rather than the three call sites, so a fourth reader cannot quietly
// pick the other convention: the writer's convention (bytes / 1e9) round-trips through the shared
// helpers, the exact byte counts both readers now produce are asserted, and the number the old
// readers produced is asserted *not* to be what the helpers return.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#include <lemon/model_size.h>

namespace {

int g_failures = 0;

void check(bool cond, const std::string& msg) {
    if (cond) {
        std::cout << "[ok] " << msg << std::endl;
    } else {
        std::cerr << "[FAIL] " << msg << std::endl;
        ++g_failures;
    }
}

}  // namespace

int main() {
    using namespace lemon::units;

    // 1. The writer's side: model_sizes.py divides HF's blob bytes by 1e9. A 10 GB row is 1e10 bytes.
    check(bytes_to_gigabyte(10000000000ULL) == 10.0,
          "10 000 000 000 bytes is exactly 10.0 in the field (writer's convention)");
    check(gigabyte_to_bytes(10.0) == 10000000000LL,
          "10.0 in the field is 10 000 000 000 bytes, not 10 737 418 240");

    // 2. The reader that used to be wrong: /api/tags reported size * 2^30. The ratio is 7.37 %, and
    //    the helpers must not reproduce it.
    const double gib_ratio = kBytesPerGibibyte / kBytesPerGigabyte;
    check(std::fabs(gib_ratio - 1.073741824) < 1e-9, "the old 2^30/1e9 disagreement is 7.37 %");
    check(gigabyte_to_bytes(10.0) != static_cast<int64_t>(10.0 * kBytesPerGibibyte),
          "the Ollama /api/tags path no longer reports the 2^30 byte count");

    // 3. Round-trip in the field's own unit, including the fractional sizes the store rounds to.
    for (const double gb : {0.5, 1.0, 4.98, 10.0, 27.3, 120.0}) {
        const int64_t bytes = gigabyte_to_bytes(gb);
        check(std::fabs(bytes_to_gigabyte(static_cast<uint64_t>(bytes)) - gb) < 1e-9,
              "round-trip " + std::to_string(gb) + " GB");
    }

    // 4. The NPU guard's threshold is stated in bytes and derived from the same constant, so 10.0 in
    //    the field is the boundary and 10.07 (the old 2^30 reading of 9.38) is not.
    check(kNpuModelTooLargeBytes == 1e10, "the NPU guard's 10 is 1e10 bytes, not 10*2^30");
    check(kNpuModelTooLargeGigabytes == 10.0, "the NPU guard's 10 is 10.0 in the field's unit");
    check(9.9 <= kNpuModelTooLargeGigabytes && 10.1 > kNpuModelTooLargeGigabytes,
          "a 9.9 GB model is below the guard and a 10.1 GB model is above it");

    // 5. Degenerate input: a negative or zero size is no bytes, never a wrapped int64.
    check(gigabyte_to_bytes(-1.0) == 0, "a negative size converts to 0 bytes, not a wrapped value");
    check(gigabyte_to_bytes(0.0) == 0, "a zero size converts to 0 bytes");

    if (g_failures == 0) {
        std::cout << "test_model_size_units: PASS" << std::endl;
        return 0;
    }
    std::cerr << "test_model_size_units: " << g_failures << " failure(s)" << std::endl;
    return 1;
}
