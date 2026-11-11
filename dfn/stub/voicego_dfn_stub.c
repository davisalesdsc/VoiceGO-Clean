// Stand-in for the DeepFilterNet library, used only to test the plugin's
// plumbing (resampling, alignment, latency) without the neural network:
// behaves like an ideal "enhancer" that returns its input delayed by exactly
// the DeepFilterNet3 delay (1440 samples at 48 kHz, hops of 480).
#include "../voicego_dfn.h"
#include <stdlib.h>
#include <string.h>

#define STUB_HOP 480
#define STUB_DELAY 1440

struct VgDfn
{
    float ring[STUB_DELAY + STUB_HOP];
    size_t pos;
};

VgDfn* vg_dfn_create (float atten_lim_db)
{
    (void) atten_lim_db;
    return (VgDfn*) calloc (1, sizeof (VgDfn));
}

size_t vg_dfn_hop_size (const VgDfn* s)    { return s ? STUB_HOP : 0; }
size_t vg_dfn_sample_rate (const VgDfn* s) { return s ? 48000 : 0; }
size_t vg_dfn_delay (const VgDfn* s)       { return s ? STUB_DELAY : 0; }

float vg_dfn_process (VgDfn* s, const float* in, float* out)
{
    const size_t len = STUB_DELAY + STUB_HOP;
    for (size_t i = 0; i < STUB_HOP; ++i)
    {
        s->ring[(s->pos + STUB_DELAY) % len] = in[i];
        out[i] = s->ring[s->pos];
        s->pos = (s->pos + 1) % len;
    }
    return 40.0f;
}

void vg_dfn_set_atten_lim (VgDfn* s, float db) { (void) s; (void) db; }
void vg_dfn_destroy (VgDfn* s) { free (s); }
