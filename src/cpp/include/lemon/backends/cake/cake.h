#pragma once

#include "lemon/backends/backend_descriptor.h"

namespace lemon { namespace backends { namespace cake {

inline const BackendDescriptor descriptor = {
    /*recipe*/              "cake",
    /*display_name*/        "Cake (Rust heterogeneous runtime)",
    /*binary*/              "lemonade-cake-bridge",
    /*config_section*/      "cake",
    /*default_device*/      DEVICE_GPU,
    /*slot_policy*/         SlotPolicy::Standard,
    /*selectable_backend*/  false,
    /*uses_ctx_size*/       false,
    /*dynamic_models*/      false,
    /*options*/ {
        {"cake_args", "--cake-args", "", "ARGS",
         "Cake command template or extra bridge arguments", "Cake Options"},
        {"cake_port", "--cake-port", "18080", "PORT",
         "Private Cake port used behind the Rust bridge", "Cake Options"},
    },
    /*support*/ {},
    /*supported_modes*/ {"chat"},
    /*required_checkpoints*/ {"main"},
    /*default_capabilities*/ {},
};

}}}  // namespace lemon::backends::cake
