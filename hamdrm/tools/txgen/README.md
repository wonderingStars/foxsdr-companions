# hamdrm_txgen - HamDRM test-signal generator

Stand-alone command-line transmitter for the HamDRM helper's receiver tests. The signal is made by **QSSTV's own transmit
chain** (ON4QZ/QSSTV, `src/drmtx/`, from PA0MBO's ham adaptation of Dream; GPL, see `CHANGES.md`), so a receiver can be
tested against the same upstream constants. Windows, MSVC 2022, CMake, C++17, no Qt, no FFTW (uses `hamdrm/src/fftw3.h`), no network.

## Build

    cmake -S hamdrm/tools/txgen -B <builddir> -G "Visual Studio 17 2022" -A x64
    cmake --build <builddir> --config Release --target hamdrm_txgen --parallel 4

or `add_subdirectory(tools/txgen)` from `hamdrm/CMakeLists.txt` (target `hamdrm_txgen`; it sets nothing globally).

## Run

    hamdrm_txgen --in <payload> --name <base.ext> --out <signal.raw>
        [--mode A|B|E] (B)  [--bw 0|1] (0)  [--qam 0|1|2] (1)  [--prot 0|1] (0)  [--interleaver 0|1] (1)
        [--callsign CALL] (TEST)  [--lead-silence-ms N]  [--tail-silence-ms N]  [--snr-db X --seed N]  [--repeat N]
        [--rate 12000|48000] (12000)  [--report]  [--dump-mot <text file>]

* `--qam`: 0 = 4-QAM, 1 = 16-QAM, 2 = 64-QAM (MSC). `--interleaver`: 0 = long (2 s), 1 = short (400 ms). `--prot`: 0 = high, 1 = normal.
  `--bw`: spectrum occupancy 0 or 1 (QSSTV SO_0 / SO_1). `--callsign` goes into the FAC service label.
* `--name` must be `base.ext`; QSSTV sends `<base>.<suffix>` as the MOT ContentName (it hands base name and suffix to the encoder separately).
* Output: mono 16-bit little-endian raw samples, no header. Fully deterministic (no clock, no unseeded randomness).
* `--snr-db X`: white gaussian noise (mt19937_64 + Box-Muller, seed `--seed`, default 1), sigma = sqrt(mean square of the
  transmission / 10^(X/10)), measured on the output samples of the transmission only and added over the **whole** output
  incl. the silences. The SNR is therefore against the full 0..6000 Hz band of the 12 kHz file, not against a 2.3 kHz bandwidth.
* `--repeat N`: N complete transmissions back to back (a new encoder per transmission, same transport id). See "Repeats" below.
* `--report`: frames, duration, rms/peak, carriers expected for the mode, and the energy per 100 Hz band.
* `--dump-mot FILE`: text file, one line per MSC data group of the first transmission (index, hex bytes).

## What the signal is (found in the upstream code)

* Rate: QSSTV's transmit chain works at **48 kHz** (`SOUNDCRD_SAMPLE_RATE` 48000, `drmtx/common/tables/TableDRMGlobal.h`), not 12 kHz.
  The 12 kHz seen in QSSTV is the receiver's decimated rate (`SAMPLERATE = 48000/4`, 180-tap FIR `downSample4()`, passband 0..2800 Hz,
  stop band from 3500 Hz; then `drmHilbertFilter` and the demodulator with Tu = 288/256/160 for modes A/B/E).
  `--rate 12000` (default) therefore decimates QSSTV's 48 kHz transmit stream with **exactly that receive filter** (same coefficients,
  gain normalised to 1 as in `downsampleFilter(.., scaled=true)`, output sample m = sum_j c[j] x[4m+3-j]). `--rate 48000` writes the
  transmit stream as it leaves the chain.
* Symbol: useful part N = 1152 (A), 1024 (B), 640 (E) samples at 48 kHz, guard N/9, N/4, N/2, so the symbol is 1280, 1280, 960 samples;
  15 (A, B) or 20 (E) symbols per frame: **19200 samples = 400 ms in every mode** (verified: 19200 samples per frame in all runs).
  Carrier spacing 41.667 / 46.875 / 75 Hz. Frames carry one MSC logical frame each; a super-frame is 3 frames (SDC is not sent, ham modes).
* Spectral position: `COFDMModulation` places carrier 0 (DC) at the "virtual IF" 6000 Hz of the cell map and then rotates the symbol down by
  6000 - 350 Hz (`SetCarOffset(350.0)` in `CDRMTransmitter::init_base()`), so **carrier k sits at 350 Hz + k * spacing**. `CTransmitData` (OF_REAL_VAL)
  takes the **real part** x 1000 and writes stereo frames [real, 0]. The tool keeps the left (real) channel. Used carriers (`TableCarrier.h`):
  A: k = 2..54 (bw 0) / 2..58 (bw 1); B: 1..45 / 1..51; E: 1..29 / 1..31. That is 433..2600 / 433..2767 Hz (A), 397..2459 / 397..2741 Hz (B),
  425..2525 / 425..2675 Hz (E): about 2.1-2.2 kHz (bw 0) and 2.3-2.4 kHz (bw 1) of carriers, not exactly the 2.3 / 2.5 kHz nominal figures.
  Measured per-100 Hz energy (`--report`): 99.4-99.6 % of the energy lies within one carrier spacing of that range; the rest is OFDM side-lobes
  (about -15 dB at +100 Hz, -40 dB at about 800 Hz above the top carrier, i.e. visible to about 3.3 kHz). QSSTV never applies its band-pass filter
  object on transmit (it is only initialised), so these side-lobes are part of the upstream signal. The 3 continuous frequency pilots are the
  strongest lines (e.g. mode B: 725, 1475, 1850 Hz).
* Receiver side (read for orientation, not used): QSSTV's receiver has no fixed IF: `getfoffsint()` finds the integer carrier offset from the
  frequency pilots over all Tu bins and the guard-interval correlation gives the fraction, so 350 Hz (= 7.47 bins in mode B) has to be found by
  the receiver, anywhere in the analytic 0..6 kHz.
* Level: 16-bit samples, output rms 4400..5750 (peak 19000..28100 over all 18 mode/bandwidth/QAM combinations tested, 48 kHz stream peak
  checked against 32767 by the tool, which prints a warning on overflow of QSSTV's own `(short)` cast).

## Repeats and length

QSSTV sends the MOT object **once per transmission** (`prepareSegmentList(1)`) as a list of data groups, one group per 400 ms frame:
run-in of 24 slots (all header segments, then the last body segment as a "size announcement" counted as a slot, then body segments 0,1,2..; the
pattern restarts until 24 slots are filled), the body 0..n-1 with header + last-segment inserted again before every segment whose index is a multiple of 50, and
a run-out of 10 slots built like the run-in. After the list is used the encoder sets `allDataSend` and QSSTV sends the list's first entries again until
`stopDRM` after 5 further data groups. **Observed in every run: frames = number of data groups in the list + 4** (printed by the tool as "N data groups in one
transmission list", e.g. 2048 bytes in mode B, bw 0, 16-QAM: 21 body segments, list 60, 64 frames = 25.6 s). Example list for 21 body segments (seen in `--dump-mot`, all 66 groups' CRC-16 re-checked with an independent implementation):
`H L 0..20 | H L 0 | H L 0..20 | H L 0..8` (H = header group, L = the last body segment 20 sent early; then `H L 0 1` are the 4 trailing frames). `--repeat N` simply produces N complete transmissions
back to back; QSSTV itself does not repeat on its own, repetition is the user pressing send again.

## MOT byte stream (from drmtx/common/datadecoding/DABMOT.cpp, DataDecoder.cpp)

Use `--dump-mot` to get the actual bytes (checked below against that dump for `--name test.txt`, 2048 bytes, mode B, bw 0, 16-QAM).

1. **Data packet** (`CDataEncoder::GeneratePacket`): one packet per frame, 24 bits + 8 * PacLen bits = exactly `iNumDecodedBitsMSC`
   (PacLen = iNumDecodedBitsMSC/8 - 3; 109 bytes in the example). Byte 0 = first(1) last(1) packet-id(2) PPI(1) continuity(3);
   every group fits one packet, so first = last = 1, continuity counts modulo 8. If the group is shorter than PacLen then PPI = 1, the next byte is the
   number of useful bytes and the rest is zero padding. Last two bytes = CRC-16 of the packet.
2. **MSC data group** (`GenMOTObj`), big-endian bit order:
   `0x73` (header) or `0x74` (body) = extension flag 0, CRC flag 1, segment flag 1, user-access flag 1, type 3 (header) / 4 (body);
   byte 1 = continuity index (4 bits, counted per type, modulo 16, +1 for every group generated) and repetition index 0;
   session header: **last-segment flag (1 bit) + segment number (15 bits)**; user-access field `0x12` (rfa 0, transport-id flag 1, length 2) + **transport id** (16 bits);
   then the segment: repetition count 0 (3 bits) + segment size (13 bits) + segment data; finally CRC-16 (X.25, init all ones, inverted) over everything before it.
   Example, first line of the dump: `73 00 | 80 00 | 12 4c40 | 0019 | <25 header bytes> | 3d4a`.
3. **Header segment (group type 3)**: header segments are cut at 98 bytes; for names up to 80 characters it is one segment, segment number 0, last flag 1. MOT header (17 + len(name) bytes):
   core 7 bytes = BodySize (28 bit) + HeaderSize (13 bit) + ContentType (6 bit) + ContentSubType (9 bit);
   then `85 00 00 00 00` (TriggerTime: PLI 2, param id 5, value 0), `46 00` (VersionNumber: PLI 1, id 6, value 0),
   and the **ContentName: `CC`, length byte (7 bits, 0 = no extension flag, = 1 + len(name)), `00` (charset: EBU Latin), name bytes**
   (param id 12, PLI 3). Name = `<base>.<ext>` as given, cut to 80 bytes. Example header bytes (25):
   `00 00 80 00 0c 84 01 | 85 00 00 00 00 | 46 00 | cc 09 00 74 65 73 74 2e 74 78 74`.
   Content type 2 (general data), subtype 1 for jpg, jpeg, jfif, jp2, rs1..rs4 and for every unknown suffix (e.g. txt), 0 for gif, 2 for bmp, 3 for png.
4. **Body segments (group type 4)**: the file bytes cut into segments of `iNumDecodedBitsMSC/8 - 14` bytes (98 in the example; group = segment + 11 bytes = 109 = PacLen);
   segment numbers 0..n-1, **last-segment flag set only on segment n-1** (the short last segment: 2048 - 20 * 98 = 88 bytes, `00 58`, group length 99, flag+number `80 14`).
5. **Transport id** (the "ham code"): over the (<= 80 byte) name: `x ^= c`; `a = (a + c) ^ k` (8 bit, k = index); id = 256 * a + x; if id <= 2 then id += len(name).
   `test.txt` -> 0x4C40 (checked against an independent re-computation); the tool prints the id.

## Left out / not verified

Reed-Solomon `.rs1..rs4` files, hybrid/FTP, BSR/FIX, GUI, the real sound card chain (synthesiser). Verified here only by self-consistency (spectrum, lengths,
determinism, MOT bytes read back); **no real receiver (QSSTV or the helper receiver) has decoded these files yet**. The FFT stand-in
`hamdrm/src/fftw3.h` is exercised by the IFFT sizes 1152, 1024 and 640 (spectrum confined to the carrier range and pilot lines where expected).
