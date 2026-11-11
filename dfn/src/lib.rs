//! Minimal C API around DeepFilterNet3 for VoiceGO Clean.
//!
//! One instance = one mono stream at 48 kHz, processed in hops of
//! `vg_dfn_hop_size()` samples (480). Panics never cross the FFI boundary.

use std::panic::{catch_unwind, AssertUnwindSafe};

use df::tract::{DfParams, DfTract, ReduceMask, RuntimeParams};
use ndarray::{ArrayView2, ArrayViewMut2};

pub struct VgDfn {
    m: DfTract,
}

/// Creates a DeepFilterNet3 instance with the embedded model.
/// `atten_lim_db`: maximum attenuation in dB (100 = unlimited).
/// Returns NULL on failure.
#[no_mangle]
pub extern "C" fn vg_dfn_create(atten_lim_db: f32) -> *mut VgDfn {
    let r = catch_unwind(AssertUnwindSafe(|| {
        let rp = RuntimeParams::default_with_ch(1)
            .with_atten_lim(atten_lim_db)
            .with_thresholds(-15.0, 35.0, 35.0)
            .with_post_filter(0.0)
            .with_mask_reduce(ReduceMask::MAX);
        let m = DfTract::new(DfParams::default(), &rp).ok()?;
        Some(Box::into_raw(Box::new(VgDfn { m })))
    }));
    match r {
        Ok(Some(p)) => p,
        _ => std::ptr::null_mut(),
    }
}

/// Samples per hop (input and output of `vg_dfn_process`).
#[no_mangle]
pub extern "C" fn vg_dfn_hop_size(st: *const VgDfn) -> usize {
    unsafe { st.as_ref() }.map(|s| s.m.hop_size).unwrap_or(0)
}

/// Model sample rate (48000).
#[no_mangle]
pub extern "C" fn vg_dfn_sample_rate(st: *const VgDfn) -> usize {
    unsafe { st.as_ref() }.map(|s| s.m.sr).unwrap_or(0)
}

/// Algorithmic delay in samples at the model rate, as derived from the model
/// config: output sample n is the enhanced version of input sample n - delay.
/// (The plugin measures it once more at start-up; see NeuralVoice.cpp.)
#[no_mangle]
pub extern "C" fn vg_dfn_delay(st: *const VgDfn) -> usize {
    unsafe { st.as_ref() }
        .map(|s| s.m.fft_size - s.m.hop_size + s.m.lookahead * s.m.hop_size)
        .unwrap_or(0)
}

/// Processes one hop. Returns the local SNR estimate in dB, or -1000 on error
/// (in which case `output` is a copy of `input`).
#[no_mangle]
pub extern "C" fn vg_dfn_process(st: *mut VgDfn, input: *const f32, output: *mut f32) -> f32 {
    let Some(s) = (unsafe { st.as_mut() }) else { return -1000.0 };
    if input.is_null() || output.is_null() {
        return -1000.0;
    }
    let hop = s.m.hop_size;
    let r = catch_unwind(AssertUnwindSafe(|| {
        let inp = unsafe { ArrayView2::from_shape_ptr((1, hop), input) };
        let out = unsafe { ArrayViewMut2::from_shape_ptr((1, hop), output) };
        s.m.process(inp, out).ok()
    }));
    match r {
        Ok(Some(lsnr)) => lsnr,
        _ => {
            unsafe { std::ptr::copy(input, output, hop) };
            -1000.0
        }
    }
}

/// Sets the attenuation limit (dB).
#[no_mangle]
pub extern "C" fn vg_dfn_set_atten_lim(st: *mut VgDfn, db: f32) {
    if let Some(s) = unsafe { st.as_mut() } {
        let _ = catch_unwind(AssertUnwindSafe(|| s.m.set_atten_lim(db)));
    }
}

/// Frees an instance created by `vg_dfn_create`.
#[no_mangle]
pub extern "C" fn vg_dfn_destroy(st: *mut VgDfn) {
    if !st.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| unsafe { drop(Box::from_raw(st)) }));
    }
}
