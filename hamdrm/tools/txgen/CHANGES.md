# hamdrm_txgen: every change versus upstream QSSTV

Upstream: QSSTV, https://github.com/ON4QZ/QSSTV, commit 31c323ccb848bade1dbab0a96890073f7b9455ea (GPL-3.0; the
individual files say GPL v2 or later, the Dream-derived ones are GPL-2+; every upstream copyright/licence header is kept).

Verbatim reference copies (not compiled) are in `hamdrm/upstream/qsstv-tx/`, same sub-folders as upstream:
`drmtx/` (drmtransmitter.*, drmparams.*, config.h, the whole `common/` tree), `appdefs.h`, `utils/vector.h`,
`dsp/downsamplefilter.{h,cpp}`. The ported working copies are in this folder (`drmtx/`, `appdefs.h`, `utils/vector.h`).
Edited files may have LF line endings where upstream has CRLF (the edits were made with sed); compare with
`diff --strip-trailing-cr`.

## Files taken UNCHANGED (byte for byte, apart from line endings) and compiled

`appdefs.h`, `utils/vector.h`, `drmtx/config.h`, and everything under `drmtx/common/` that is not listed below:
CDrmTransmitter.{cpp,h}, DRMSignalIO.{cpp,h}, DataIO.{cpp,h}, GlobalDefinitions.h, OFDM.{cpp,h}, Parameter.{cpp,h},
soundinterface.h, FAC/, SDC/, datadecoding/DataDecoder.{cpp,h}, datadecoding/DABMOT.h (except line 525), interleaver/,
matlib/, mlc/, ofdmcellmapping/, tables/, util/, sourcedecoders/AudioSourceDecoder.cpp.
Not copied/compiled at all (not part of the data-file transmit path): drmtx.{cpp,h} (ftp, hybrid, BSR/FIX, GUI glue),
bsrform.*, utils/reedsolomoncoder.* and rs.* (optional .rs1..rs4 Reed-Solomon files: skipped), dsp/synthes.* (the 48 kHz
sound-card writer), everything receiver and GUI.

## Files edited (diff against `hamdrm/upstream/qsstv-tx/`)

| File | Change | Reason |
|---|---|---|
| `drmtx/drmtransmitter.h` | `init(QByteArray*, QString, QString, drmTxParams)` becomes `init(std::vector<unsigned char>*, std::string, std::string, drmTxParams)` | no Qt |
| `drmtx/drmtransmitter.cpp` | same signature; `ba->count()` -> `(int)ba->size()`; `params.callsign.toLatin1().data()` -> `params.callsign`; `#include "configparams.h"` removed | no Qt; configparams.h is QSSTV's settings header |
| `drmtx/drmparams.h` | `<QString>`/`<QList>` -> `<string>`/`<vector>`; `QString callsign` -> `std::string`; `QList<short unsigned int> fixBlockList` -> `std::vector<unsigned short>` | no Qt |
| `drmtx/drmparams.cpp` | `#include "configparams.h"` -> `"appglobal.h"` (compat stub, gives `uint`); `fixBlockList` type as in the header; `prm.callsign=myCallsign` -> `prm.callsign=""` in `modeToParams()` (unused by the tool; `myCallsign` is a QSSTV settings global) | no Qt / no settings |
| `drmtx/common/datadecoding/DABMOT.h` | `QList <int> segmentList;` -> `std::vector<int> segmentList;` | no Qt |
| `drmtx/common/datadecoding/DABMOT.cpp` | `#include <QDebug>` removed; `segmentList.append(x)` -> `push_back(x)` (12 places); `.count()` -> `.size()` (3 places: `fixBlockList` twice, `segmentList` once); `#include "drm.h"` added; **bug fix**: `strFormat.resize(NewMOTObject.strFormat.size());` inserted before the `transform(... tolower)` call (upstream transforms into an EMPTY `std::string`, writing past its end: undefined behaviour that only works by accident for short suffixes; the intended result is unchanged); **test hook**: at the end of `CMOTDABEnc::GetDataGroup()` the generated data group is packed into bytes and appended to `*txGroupDump` when that pointer is non-null (used by `--dump-mot`; no effect on the signal) | no Qt; UB; tool feature |
| `drmtx/common/datadecoding/MOTSlideShow.h` | `QByteArray*` -> `std::vector<unsigned char>*`, `QString name/format` -> `std::string` (struct `SPicDescr` and `AddArray`) | no Qt |
| `drmtx/common/datadecoding/MOTSlideShow.cpp` | `QString tmp` -> `std::string tmp`; `.toLatin1().data()` / `.toLatin1()` dropped (3 places); `arrayPtr->count()` -> `(int)arrayPtr->size()`; `AddArray` signature as in the header | no Qt |
| `drmtx/common/sourcedecoders/AudioSourceDecoder.h` | both `SetPicFileName(QByteArray*, const QString, const QString)` -> `(std::vector<unsigned char>*, const std::string, const std::string)` | no Qt |
| `drmtx/common/csoundout.h` | `<vector>` included; declares `extern std::vector<short> txSamples48k; extern int txFramesWritten;` | replaces the sound card by an in-memory buffer |
| `drmtx/common/csoundout.cpp` | `synthes.h`/`vector.h` includes removed; defines the two globals; `CSoundOut::Write()` no longer calls `synthesPtr->writeBuffer(...)`: it appends the left (= real) channel of every stereo frame to `txSamples48k` and counts the call (one call = one 400 ms frame) | no sound card, no QSSTV synthesiser |

No change at all (compiles and behaves as upstream) in: OFDM.cpp, DRMSignalIO.cpp, Parameter.cpp, FAC, SDC, mlc,
interleaver, ofdmcellmapping, matlib (apart from the `<fftw3.h>` redirect below), tables, util, CDrmTransmitter.cpp.
Behaviour quirks of upstream that were deliberately kept: the band-pass filter object in CTransmitData is initialised
but never applied; `rNormFactor` is overwritten with 1000.0 in `ProcessDataInternal`; `CDRMTransmitter::init_base()`
selects mode E / SO_1 / 16-QAM / long interleaver before `drmTransmitter::init()` overrides them.

## Stand-in / new files (not from QSSTV, GPL-3.0-or-later, written for the helper)

| File | Purpose |
|---|---|
| `compat/appglobal.h` | replaces QSSTV's appglobal.h: `typedef uint/ulong/quint32`, `#define addToLog(...) ((void)0)` (the macro drops its arguments, so the many `addToLog(QString(...))` call sites need no edit), the `LOG*` enum names, includes appdefs.h |
| `compat/drm.h`, `compat/txglobals.cpp` | replace the receiver's drm.h/drm.cpp for the globals the transmitter uses: `bool stopDRM`, `uint txTransportID` (QSSTV: drmrx/drm.cpp), plus the tool's `txGroupDump` pointer |
| `compat/supportfunctions.h` | empty (QSSTV's header is included by CDrmTransmitter.cpp but nothing from it is used) |
| `compat/fftw3.h` | forwards `#include <fftw3.h>` (used by matlib/MatlibStdToolbox.h) to `hamdrm/src/fftw3.h`, the helper's header-only FFTW stand-in. Relative include on purpose: `hamdrm/src` also holds the receiver's own appdefs.h/drm.h, which must not shadow these stubs |
| `dsp/downsample_coeffs.cpp` | only the 180-tap coefficient table `downSampleFilterParam` of QSSTV's receive down-sampler (dsp/downsamplefilter.cpp, numbers unchanged, original licence header kept) with an `extern` declaration for external linkage; the Qt-dependent filter class is dropped |
| `txgen_main.cpp` | the command-line driver (argument parsing, payload, QSSTV-style `drmTransmitter::init()/start()`, 48 -> 12 kHz decimation with QSSTV's own filter, silence, noise, `--report`, `--dump-mot`) |
| `CMakeLists.txt` | target `hamdrm_txgen`, C++17, include directories only for this target, no find_package, no global settings |

## Things NOT done (skipped on purpose)

Reed-Solomon `.rs1..rs4` wrapper files (`--reedsolomon` of QSSTV), hybrid/FTP, BSR/FIX re-send blocks (`fixBlockList`
stays empty, so the full segment list is sent), GUI, sound card output, the 48 kHz synthesiser.
