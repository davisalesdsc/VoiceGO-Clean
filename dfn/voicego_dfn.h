// SPDX-License-Identifier: MIT OR Apache-2.0
// C API of the voicego_dfn static library (DeepFilterNet3 wrapper). See dfn/src/lib.rs.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VgDfn VgDfn;

VgDfn* vg_dfn_create (float atten_lim_db);
size_t vg_dfn_hop_size (const VgDfn*);
size_t vg_dfn_sample_rate (const VgDfn*);
size_t vg_dfn_delay (const VgDfn*);
float  vg_dfn_process (VgDfn*, const float* input, float* output);
void   vg_dfn_set_atten_lim (VgDfn*, float db);
void   vg_dfn_destroy (VgDfn*);

#ifdef __cplusplus
}
#endif
