# iir1 (vendored)

DSP IIR realtime filter library by Vinnie Falco and Bernd Porr. MIT license (see COPYING).

- Upstream: https://github.com/berndporr/iir1
- Vendored from commit `bb21b64ea2daa9eda92e57763e21c0e322411f34` (2025-07-07).
- Only `Iir.h`, `iir/`, and `COPYING` are vendored (demo/docs/tests/CMake omitted);
  the sources are compiled directly into the modules that use them.
- **No local modifications** - keep it that way if possible, so upstream updates
  are a straight copy.

This is the maintained successor of the Vinnie Falco "DspFilters" library that the
deprecated Butterworth modules (modules/Filters) are built on. Differences that matter
here:

- per-sample `filter()` processing instead of block `process()`;
- `setupN()` takes normalised frequencies (0..0.5) directly;
- no denormal-prevention DC offset injected into the signal path (DspFilters' `ac()`
  hack caused low-level noise/DC in the gen-2 modules);
- the odd-order high-pass polarity inversion that the SE-local DspFilters copy
  patches (see modules/Filters/DspFilters/Cascade.cpp and
  SE16/tests/butterworth_filter_tests.cpp) does NOT occur in iir1 - verified
  behaviourally at vendor time: `response(0.5)` is real-positive and a passband
  tone comes out in phase for orders 1..12. Re-check when updating upstream.
- designs requiring optimisation (Elliptic, Bessel, Legendre) were removed
  upstream - the oversampler's Elliptic low-pass still uses the old DspFilters copy.
