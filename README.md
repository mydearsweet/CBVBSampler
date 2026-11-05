# CBVB Sampler

Polyphonic sampler (8 voices) -> ADSR -> combined Pultec EQP-1A + MEQ-5 EQ -> analog tape delay -> limiter.
JUCE 8.0.4, VST3 + Standalone (+ AU on macOS).

## Build
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release          # fetches JUCE 8.0.4
    cmake --build build --config Release --target CBVBSampler_VST3
    # or: -DJUCE_DIR=/path/to/JUCE to use a local checkout; target CBVBSampler_Standalone for the app

Linux needs: libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev
libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev

## Files
Drop or LOAD: .wav .aif .flac .ogg .ncw .sfz .dspreset.

- **.ncw** (Native Instruments Compressed Wave): plain, unprotected NCW audio is decoded losslessly
  (16/24-bit PCM and 32-bit float, mono/stereo). Source/NcwDecoder.h is an independent implementation of the
  publicly documented format and was checked sample-for-sample against reference .ncw/.wav pairs.
  NCW inside encrypted/locked Kontakt libraries is not supported.
- **.sfz**: regions are honoured for sample, key / lokey / hikey / pitch_keycenter (numbers or note names like c4, f#3),
  lovel / hivel, <control> default_path, <global>/<master>/<group>/<region> inheritance, #define, #include and comments.
  Samples may be .wav/.flac/.ogg/.aif/.ncw. Regions with trigger=release, '*generator' samples or hikey=-1 are skipped.
  Loops, offsets, envelopes, filters, keyswitches, round-robin and every other opcode are ignored.
- **.dspreset**: sample zones (rootNote, loNote/hiNote, loVel/hiVel) are honoured; loops, DS effects and DS UI are not.
- Kontakt instruments (.nki etc.) are not read directly: convert them to .sfz/.dspreset first (e.g. ConvertWithMoss).

Single audio files follow the ROOT NOTE knob; .sfz/.dspreset zones use their own root notes.

## Velocity mode
The VOICE & TUNING panel includes a two-position VELOCITY MODE switch.
- TOUCH SENSITIVE (default): MIDI velocity determines note loudness and selects velocity-layer zones in SFZ/DecentSampler instruments.
- FIXED VELOCITY: every note uses MIDI velocity 100 for both playback level and sample-zone selection, independent of key pressure. Useful for Mellotron-style samples.
This setting is automatable and saved with plugin state.

## Colours
All GUI colours come from the 16-colour palette in `palette::at()` (Source/PluginEditor.h), plus one dark ink
colour (#3B322C) for text/ticks and one error colour. Knobs cycle through the palette in order.

## Tuning the EQ voicing (PultecMeqEq.h)
kAttenCornerRatio, kLowShelfQ, kHiAttenQ, kLowPeakQ, kDipQ, kHiPeakQ, kTubeDrive.

## Verified (headless test, Linux/gcc 13)
Pitch tracking (+12 st = 2x, 44.1k file at 48k host), .dspreset zone roots, 40-note voice-steal storm finite and
limited <= 1.0, EQ responses at the expected frequencies/gains, delay time exact, delay stable at 95% fb / 100% drive / 3% wow.
.ncw decoder: bit-exact against 6 reference .ncw/.wav pairs (16/24-bit, mono/stereo, 32-bit float, multi-block).
.sfz / .ncw / .dspreset loading driven through the real PluginProcessor::loadFile() on Linux.
Not tested: inside a DAW, pluginval, macOS builds, NCW mid/side stereo blocks (no real file available to check),
24-bit stereo NCW against a reference wav (no reference wav exists; it decodes cleanly).
The Windows VST3 was cross-compiled from Linux (clang/MinGW), not built with MSVC.

JUCE is AGPLv3/commercial - check licensing before distributing binaries.
