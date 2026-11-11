// Build without the neural model: vg_dfn_create() fails, so the plugin uses
// its DSP separation path.
#include "../voicego_dfn.h"

VgDfn* vg_dfn_create (float atten_lim_db) { (void) atten_lim_db; return 0; }
size_t vg_dfn_hop_size (const VgDfn* s)    { (void) s; return 0; }
size_t vg_dfn_sample_rate (const VgDfn* s) { (void) s; return 0; }
size_t vg_dfn_delay (const VgDfn* s)       { (void) s; return 0; }
float  vg_dfn_process (VgDfn* s, const float* i, float* o) { (void) s; (void) i; (void) o; return -1000.0f; }
void   vg_dfn_set_atten_lim (VgDfn* s, float db) { (void) s; (void) db; }
void   vg_dfn_destroy (VgDfn* s) { (void) s; }
