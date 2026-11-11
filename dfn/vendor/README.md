# Vendored crates

`tract-linalg/` is tract-linalg 0.21.4 from crates.io (MIT OR Apache-2.0,
https://github.com/sonos/tract), unchanged except for `has_amx()` in
`src/arm64.rs`, which now returns `false`: upstream assumes every arm64 Mac
exposes Apple's undocumented AMX coprocessor, which makes the plugin crash
with SIGILL where it does not (e.g. virtualised macOS). NEON kernels are used
instead.
