#pragma once
// onebit: the 1bit engine (github.com/1bit-MONSTER/engine) as a backend.
//
// The engine is one binary, `1bit`, that serves one model behind an
// OpenAI-compatible API on whichever device runs it: `1bit serve -m <model>
// --port <p> --device <vulkan|hrx|npu|zinc>`. Lemonade downloads and resolves
// GGUF checkpoints exactly as for llamacpp, launches `1bit serve` on the
// selected backend, and forwards OpenAI requests to it. `1bit` is installed
// separately (like flm); its path comes from the onebit_bin option, then
// $LEMONADE_ONEBIT_BIN, then `1bit` on PATH.
#include "lemon/backends/backend_descriptor.h"

#include <nlohmann/json.hpp>

namespace lemon {
namespace backends {
namespace onebit {

inline const BackendDescriptor descriptor = {
    /*recipe*/          "onebit",
    /*display_name*/    "1bit engine (NPU, Vulkan, HRX, CUDA)",
    /*binary*/          "1bit",
    /*config_section*/  "onebit",
    /*default_device*/  DEVICE_GPU,
    /*slot_policy*/     SlotPolicy::Standard,
    /*selectable_backend*/ true,   // onebit_backend / --onebit: the device 1bit serve runs on
    /*uses_ctx_size*/   true,      // passed to 1bit serve as --ctx-size
    /*dynamic_models*/  false,
    /*options*/ {
        {"onebit_backend", "--onebit", "", "BACKEND",
         "Device 1bit serve runs the model on: auto, vulkan, hrx, npu or cuda (NVIDIA, through ZINC)", "1bit Options"},
        {"onebit_bin", "--onebit-bin", "", "PATH",
         "Path to the 1bit binary (default: $LEMONADE_ONEBIT_BIN, then 1bit on PATH)", "1bit Options"},
        {"onebit_args", "--onebit-args", "", "ARGS",
         "Extra arguments appended to the 1bit serve command line", "1bit Options"},
    },
    /*support*/ {
        {"vulkan", {"linux"}, {{"amd_gpu", {}}}, "AMD GPUs through Vulkan"},
        {"hrx", {"linux"}, {{"amd_gpu", {"gfx1151"}}}, "Strix Halo iGPU through AMD's HRX"},
        {"npu", {"linux"}, {{"amd_npu", {"XDNA2"}}}, "XDNA2 NPU (1bit NPU model directories)"},
        {"cuda", {"linux"}, {{"nvidia_gpu", {"sm_89", "sm_120"}}}, "NVIDIA Ada and Blackwell GPUs (through ZINC)"},
    },
    /*supported_modes*/ {"chat"},
    /*required_checkpoints*/ {"main"},
    /*default_capabilities*/ {},
    /*experimental*/    true,
    /*web_display_name*/ "1bit engine",
    /*rocm_channels*/   {},
    /*exposes_prometheus_metrics*/ false,
    /*rocm_requires_cwsr_fix*/ false,
    /*version_policy*/  VersionPolicy::AtLeast,  // installed separately, like flm
    /*self_manages_downloads*/ false,            // Lemonade downloads the GGUF, as for llamacpp
    /*takes_args*/      true,
    /*arg_variants*/    {},
    /*bin_variants*/    {},
    /*config_extra*/    nlohmann::json::object(),
};

}  // namespace onebit
}  // namespace backends
}  // namespace lemon
