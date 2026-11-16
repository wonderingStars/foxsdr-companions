# Upstream: msumrcal

* Project: SatDump - https://github.com/SatDump/SatDump
* Commit taken: `9fee0793c7d42bea2d401d330422e18c33c126fa` (committed 2026-10-22, "Fix IERS crash"); read from a local clone.
* Licence: GPL-3.0 (the repository's `LICENSE` is the full GNU GPL version 3 text, copied here as `LICENSE`; GitHub reports `GPL-3.0`).
  The files taken carry no per-file licence header and no "or later" wording, so this helper is labelled `GPL-3.0-only`.
* Why SatDump: the research row "Meteor-M infrared factory calibration" named it; it is the only source found that does this calibration in code.

## Files taken (verbatim copies, for reference, in `upstream/satdump/`)
| File | Role |
|---|---|
| `plugins/meteor_support/meteor/instruments/msumr/msumr_calibrator.h` | the calibrator: reference-temperature search, outlier rule, two-point radiance |
| `plugins/meteor_support/meteor/instruments/msumr/msumr_tlm.h` | how the cold/hot temperatures are decoded from the analog telemetry frame (documented in README, not ported) |
| `src-core/common/calibration.cpp`, `calibration.h` | `temperature_to_radiance`, `radiance_to_temperature` (Planck, constants c1, c2, e) |
| `src-core/utils/stats.h` | `most_common` |
| `resources/calibration/MSU-MR.json` | the wavenumbers |

## What was ported (`src/msumr_calib.h`, about 190 lines, upstream names in comments)
* `MeteorMsuMrCalibrator` constructor, infrared branch (`can_ir_calib`) -> `build_reference()`: mean of the two cold and two hot temperatures,
  forward search to the next strip that has temperatures, backward search if still zero (upstream's `while (0 < next_x_calib)` quirk, index 0 never
  examined going backwards, is kept), `most_common` of the whole pass, replacement of values more than 5 K from it.
* `MeteorMsuMrCalibrator::compute()`, infrared branch -> `two_point()`, `pixel_radiance()`: gain = (hot_rad - cold_rad)/(hot_view - cold_view),
  rad = cold_rad + (px - cold_view) * gain, invalid when a view or the pixel is 0 or both reference temperatures are 0; the LRPT `pos_y /= 8` -> `--lines-per-tlm`.
* `temperature_to_radiance`, `radiance_to_temperature` (kept with upstream's truncated `e = 2.7182818`), `most_common` (ties go to the smaller value, as upstream).
* The brightness temperature is `radiance_to_temperature(rad, wavenumber)` as `EmRadToBrightTempConverter` (`src-core/products/image/converters/em_rad_to_bright_temp.h`) does.

## Changes and why
1. No nlohmann::json, no SatDump product/image types: inputs come from the text line format in README.md. (Different input path; the arithmetic is the same.)
2. `hot_view == cold_view` is invalid (upstream would divide by zero).
3. Non-positive radiance gives `null` instead of NaN.
4. A pixel row whose strip has no telemetry record gives `null` (upstream would read past the end of its `views` vector).
5. The M2-2 "+40 K on the cold temperatures" patch of `parseMSUMRTelemetry` is the option `--cold-offset-k`, applied by the caller who knows the unit id.
6. Left out: visible channels 1-3 (a different formula and per-satellite coefficients), the "fill missing lines" interpolation of the views in the module (`fill_missing`),
   the reader that extracts telemetry from LRPT packets (a separate program is expected to do that), HRPT mode, all image output and reprojection.

## Where the wavenumbers come from (the research asked)
`resources/calibration/MSU-MR.json` (added in SatDump commit `6e1062a8`, 2025-01-29, "Calibrated VIS on MSU-MR", by Aang23; before that the same numbers were
hard-coded in `module_meteor_msumr_lrpt.cpp` and `module_meteor_instruments.cpp`, verified with `git grep` at `6e1062a8^`):
channel 4 = 2695.9743, channel 5 = 925.4075, channel 6 = 839.8979 cm-1 (channels 1-3: 16666.67, 11111.11, 5882.35). The three infrared values are
character-for-character the `wavnb`/`Vc` values of the AVHRR/3 channels 3B, 4, 5 in `resources/calibration/AVHRR.json` (which also carries the AVHRR A, B, Ns, b
non-linearity coefficients that SatDump does NOT apply to MSU-MR). SatDump states no source for the MSU-MR use. History (`git log -S"925.4075"` on the full SatDump clone, 2026-11-12): the value first appears in `435b42d6`
(2023-11-23, "N15 calib", ZbychuButItWasTaken, the NOAA-15 AVHRR calibration), and again in `be81166d` (2024-06-23, "MSU-MR Calibration /!\", Aang23), which is the
commit that introduced it for the MSU-MR - so the AVHRR numbers predate the MSU-MR use by seven months. Two searches (2026-11-12) did not find the numbers
elsewhere: `site:github.com "2695.9743" OR "925.4075" Meteor MSU-MR wavenumber` (no GitHub hit; EUMETSAT NWP SAF spectral response pages for Meteor-M list different central
wavenumbers, e.g. 909.479 / 2739.855 cm-1 for Meteor-M N1, 911.126 / 2738.952 for N2) and a KLM User's Guide search (could not confirm the AVHRR numbers either).
**UNVERIFIED**: that these are right for the MSU-MR; I believe they are a stand-in borrowed from NOAA AVHRR/3. Brightness temperatures may be off by a few kelvin absolutely;
the hot/cold two-point scale itself does not depend on the wavenumber much for small spans, but the absolute level does.

## Build recipe
`cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release`; tests with `ctest --test-dir build -C Release`.
Toolchain: MSVC 2022, C++17, no third-party library.
