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
| Beat Rate | Hz | Beats per second of a partial at 440 Hz. Each partial beats in proportion to its own frequency, so higher harmonics and higher notes beat faster, times a fixed per-harmonic factor of 0.75..1.25 so no two harmonics share a rate. Takes Detune's old pin slot. |
| Beating | 0..1 | Level of each partial's twin, up to 0.8 of the partial at 1, so a beat dips by about 19 dB but never cancels. Takes Detune Pattern's old pin slot. |
| Release Time | s | T60 of the release fade. Playback-only, no re-bake. |
| Lowest Note, Highest Note | int | MIDI range baked (default 24..96); notes outside play the nearest sample. |
| VoiceActive | host | A re-activated voice resets to idle, so its next gate is accepted. |
| RenderMode | host | Offline render bakes synchronously. |
| Signal Out | audio | The finished note: sustain peaks at 5 V, the attack may reach 10 V. |
| Unison | int | 0..8 extra copies of every partial at +6, -6, +12, -12.. cents; pair k at 0.7^k level. In cents, because a copy stands in for a whole second string. |
| Pick Noise | float | Pick click level; 1 = RMS as loud as the string's first 50 ms. |
| Pick Tone | float | 0..1 moves the click's band centre from 1 kHz to 8 kHz (band = centre / 2.5 .. centre x 2.5). |
| Body | float | Body thump level, on the same scale as Pick Noise. |
| Stiffness | float | String stiffness B in units of 1e-4 (guitar roughly 0.1..1, piano 1..10). Partial n is sharpened by `sqrt(1 + B n^2)`, divided by the lowest partial's share so the note stays in tune. Unlike Harmonic Spacing the stretch grows with n squared and only ever goes sharp. |

Unison and later pins are appended after Signal Out so that patches saved earlier keep their pin indices.

## Bake (`FretPluckedBake.cpp`, no host types)
- Additive synthesis per note in double precision with recursive-rotation oscillators. Partials above 0.45 x sample rate are dropped, and so is a partial once it falls below -100 dB.
- Partial amplitudes decay until `freezeSeconds` (2 s), then hold, leaving a stationary tail. The loop is `[S, S+L)` with `S` = 2 s and `L` = the whole number of f0 periods just over 0.35 s. The note's `rootHz` is the f0 that makes the period exact (under 0.1 cent from nominal), so harmonic partials loop perfectly.
- Beating: every partial has a twin a little above it, both starting at phase 0, so all the beating starts in step and then drifts apart. With beating on, the loop is at least 2 s instead of 0.35 s, and each twin's offset is rounded to a whole number of beats per loop (0.5 Hz steps at 2 s, at least one), so the beating carries on through the loop unbroken. That makes the table about 1.7 times larger.
- Spaced, stretched or unison partials don't fit the loop exactly. The last 80 ms of the loop (at most half of it) is crossfaded into the frozen tail *before* `S`, so the wrap lands on a continuous waveform, and entry into the loop from the attack is untouched.
- Pick and body are short decaying sines with random phases, added to each note's first 0.5 s and gone long before the loop. The click is 48 of them, log-spread over its band with T60s of 5-40 ms. The body is 4 modes near 100, 205, 390 and 580 Hz with T60s of 60-150 ms. Their frequencies, decays and phases come from a hash of the note number: every semitone gets its own pick, yet each bake is identical. A 0.5 ms fade-in keeps the random starting phases from making a step. Each set is scaled to its level times the string's RMS over the first 50 ms.
- Guard samples: 1 before each note, 3 after the loop end. One gain sets the whole table's level, so the level relationship between notes survives. It comes from the string's peak after its first 50 ms, measured before the pick and body go in. In-phase partials make a short spike at note-on, and normalising on that made bright settings 10-20 dB quieter. Anything above 5 V is then soft-limited (tanh) to stay under 10 V; that only affects the attack.
- Multi-threaded across notes. A full 24..96 bake takes 21 ms (defaults) to 60 ms (64 slow partials) at 44.1 kHz, and 150 ms at 96 kHz. It is about 30 MB at 44.1 kHz and 66 MB at 96 kHz.

## Cache and loader (`FretPluckedCache.cpp`)
- Process-wide cache, weakly held, keyed on (Params, sample rate): every voice of a patch shares one bake.
- One background worker with a lock-free queue. It debounces by 60 ms per batch, and a superseded request is cancelled even mid-bake. Freed tables are released on the worker. The classes live in `namespace fret_plucked`, so they never collide with WavetableOsc's when modules are linked statically.

## Voice (`FretPluckedOsc.h`)
- Gate rise while idle: pick `round(69 + 12*(10*pitch - 5))`, adopt any finished re-bake, and play from 0 at gain 1. A note never changes table mid-flight: it holds its own reference to its table.
- VCA: before the loop gain is 1, so the baked decay is the envelope. In the loop it continues at the lowest partial's decay rate, scaled by playback speed. Once released it fades at the Release Time rate instead.
- The note ends when the VCA gain is below -90 dB, or when the output has been below -100 dB for 2048 samples (in the loop or released). The output then stops streaming and the module sleeps.
- A gate that rises before the first table exists starts the note as soon as the table lands.

## GUI (`FretPluckedOscGui.cpp`)
- The oscillator draws a waterfall of its A4 note on the panel, in oblique projection: time left to right along the front edge (ticked in seconds), frequency from the back (0 Hz) to the front (just above the highest partial, at most 20 kHz), loudness (amplitude^0.6, Stevens) bottom to top, so envelopes curve like a real decay while quiet harmonics stay visible; it reaches the floor at -60 dB. Each harmonic is one envelope at its true frequency, so spacing and stiffness show as uneven gaps, and its beating with its twin shows as ripples; the fundamental is yellow, at the back. The shorter higher harmonics stand in front, so little is hidden. The floor marks the 2 s freeze where the loop starts, and the time span runs until the looped tail reaches the floor (at most 12 s).
- Data path: the DSP sends its current `Params` (versioned, `envelopeBlob`) on a private blob output pin bound to parameter 0, and the GUI recomputes the partials with the bake's own `partials()`, so the display always matches the sound. SynthEdit marks a DSP-driven parameter `stateful="false"`, so nothing is saved with the patch; the display fills in once a voice has run (in a polyphonic patch, after the first note).
- Adding GUI pins renumbers the plugs (SynthEdit puts GUI pins first); older patches load correctly because the editor remaps them by pin.

## Demo patch
`FretPluckedOsc.synthedit`: MIDI In -> MIDI-CV 2 -> the oscillator -> a linear VCA as master volume -> Sound Out, with a scope and a knob or list for most parameters. A voice's attack can reach 10 V, so the volume (default 3) keeps a four-note chord's pluck from clipping.

## Not done / possible next steps
- The Pluck Position and Seed pins.
- Switching to the neighbour sample during large bends (v1 keeps the note-on sample).
- A different pick on every replay of the same note: bake two to four pick variants per note and cycle through them at note-on.
- Starting the tonal partials at hashed phases (behind the same fade-in) would remove the note-on spike that the limiter now flattens.
- int16 storage (halves memory), or baking each note lazily on its first note-on.
