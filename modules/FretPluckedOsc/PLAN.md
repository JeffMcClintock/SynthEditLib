# Fret Plucked Oscillator — design

Plugin id `SE Fret Plucked Osc`, "Fret Plucked Oscillator", category Waveform. A `gmpi::Processor` that plays a baked
multisample (one sample per semitone) of a plucked harmonic series through a built-in VCA. Background baking, the shared
cache and the offline-render path follow `WavetableOsc`; note-on sample selection follows `SamplePlayer3`.

## Pins
| Pin | Type | Notes |
|---|---|---|
| Pitch | audio | 1 V/oct, 5 V = A440. Picks the sample at note-on, then sets the playback rate (bends and vibrato resample). |
| Gate | bool | Rising edge plucks, falling edge releases. Ignored after it goes low until the note has died away. |
| Velocity | audio | Latched at note-on, 10 V = full. |
| Harmonics | int | Number of partials (1..256). |
| Series | enum | All (1,2,3..), Odd (1,3,5..), Even (2,4,6.., no fundamental: sounds an octave up). |
| Harmonic Spacing | float | Partial n sits at `(1 + (n-1)*spacing) * f0`: 1.0 = harmonic, 1.1 gives 1, 2.1, 3.2..; above 1 is bell-like. |
| Spectrum Tilt | float | Initial level `(n/lowest)^-tilt`. |
| Decay Time | s | T60 of the lowest partial. |
| Decay Tilt | float | T60 of partial n: `DecayTime / (n/lowest)^tilt`, so higher harmonics die faster. |
| Detune | cents | Average detune per partial. The lowest partial is never detuned, so the note stays in tune. |
| Detune Pattern | enum | Alternating (down, up, down..), or Random (fixed hash, magnitude 0.5..1.5 x Detune, random sign; identical on every bake). |
| Release Time | s | T60 of the release fade. Playback-only, no re-bake. |
| Lowest Note, Highest Note | int | MIDI range baked (default 24..96); notes outside play the nearest sample. |
| VoiceActive | host | A re-activated voice resets to idle, so its next gate is accepted. |
| RenderMode | host | Offline render bakes synchronously. |
| Signal Out | audio | The finished note, peak 5 V. |

## Bake (`FretPluckedBake.cpp`, no host types)
- Additive synthesis per note in double precision with recursive-rotation oscillators. Partials above 0.45 x sample rate are dropped, and so is a partial once it falls below -100 dB.
- Partial amplitudes decay until `freezeSeconds` (2 s), then hold, leaving a stationary tail. The loop is `[S, S+L)` with `S` = 2 s and `L` = the whole number of f0 periods just over 0.35 s. The note's `rootHz` is the f0 that makes the period exact (under 0.1 cent from nominal), so harmonic partials loop perfectly.
- Detuned or spaced partials don't fit the loop exactly. The last 80 ms of the loop (at most half of it) is crossfaded into the frozen tail *before* `S`, so the wrap lands on a continuous waveform, and entry into the loop from the attack is untouched.
- Guard samples: 1 before each note, 3 after the loop end. One gain normalises the whole table to 5 V peak, so the level relationship between notes survives.
- Multi-threaded across notes. A full 24..96 bake takes 21 ms (defaults) to 60 ms (64 slow partials) at 44.1 kHz, and 150 ms at 96 kHz. It is about 30 MB at 44.1 kHz and 66 MB at 96 kHz.

## Cache and loader (`FretPluckedCache.cpp`)
- Process-wide cache, weakly held, keyed on (Params, sample rate): every voice of a patch shares one bake.
- One background worker with a lock-free queue. It debounces by 60 ms per batch, and a superseded request is cancelled even mid-bake. Freed tables are released on the worker. The classes live in `namespace fret_plucked`, so they never collide with WavetableOsc's when modules are linked statically.

## Voice (`FretPluckedOsc.h`)
- Gate rise while idle: pick `round(69 + 12*(10*pitch - 5))`, adopt any finished re-bake, and play from 0 at gain 1. A note never changes table mid-flight: it holds its own reference to its table.
- VCA: before the loop gain is 1, so the baked decay is the envelope. In the loop it continues at the lowest partial's decay rate, scaled by playback speed. Once released it fades at the Release Time rate instead.
- The note ends when the VCA gain is below -90 dB, or when the output has been below -100 dB for 2048 samples (in the loop or released). The output then stops streaming and the module sleeps.
- A gate that rises before the first table exists starts the note as soon as the table lands.

## Demo patch
`FretPluckedOsc.synthedit`: MIDI In -> MIDI-CV 2 -> the oscillator -> a linear VCA as master volume -> Sound Out, with a scope and a knob or list for every parameter. Each voice peaks at 5 V, so the volume (default 4) keeps a four-note chord from clipping.

## Not done / possible next steps
- GUI display of the partials, and the Pluck Position, Seed and Detune Tilt pins.
- Switching to the neighbour sample during large bends (v1 keeps the note-on sample).
- int16 storage (halves memory), or baking each note lazily on its first note-on.
