# Third-party notices

## DeepFilterNet (libDF + DeepFilterNet3 model)

VoiceGO Clean embeds the DeepFilterNet3 speech-enhancement model and its Rust
runtime (libDF), https://github.com/Rikorose/DeepFilterNet, used under the MIT
license (dual-licensed MIT OR Apache-2.0).

    Copyright (c) Hendrik Schröter

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

The Rust crates it depends on (tract, ndarray, rustfft, ...) are MIT and/or
Apache-2.0 licensed; see dfn/Cargo.lock for the exact list.

## tract-linalg (patched copy in dfn/vendor/tract-linalg)

tract-linalg 0.21.4 (https://github.com/sonos/tract), MIT OR Apache-2.0, vendored
with one change: Apple AMX kernels are disabled (`has_amx()` returns false) so the
plugin only uses documented NEON instructions on Apple Silicon. Its license files
are kept in that folder.

## JUCE

Built with JUCE 8 (https://juce.com), used under the terms of the JUCE license (AGPLv3 or a commercial JUCE license).

## Test audio (not shipped)

The CI tests download speech/noise/room samples from the DeepFilterNet repository
(freesound.org, CC BY-NC 3.0 / CC BY 3.0 / CC0). They are not part of the plugin.
