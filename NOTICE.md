# Licence index

Each folder is a separate program. The licence of a folder is the one in its own `LICENSE` file (and the notices in its
source files); this table only points to it. Where a folder contains code of another project (a `third_party/` or
`upstream/` folder), that code keeps its own licence and notices, listed in the folder's `UPSTREAM.md`.

| Folder | Licence as the folder states it | Upstream project |
|---|---|---|
| `atn` | GPL-3.0-or-later (also `LICENSE.asn1c`, `third_party/` licences) | dumpvdl2, libacars, asn1c |
| `fldigimodes` | GPL-3.0-only | fldigi |
| `hamdrm` | GPL-3.0-or-later (also `COPYING`, QSSTV's request for attribution) | QSSTV |
| `hfdl` | GPL-3.0-or-later (libfec: LGPL, text in `src/libfec/COPYING.LIB`; liquid-dsp and libacars licences in the folder) | dumphfdl |
| `hfmodes` | GPL-3.0-or-later with the additional terms in `ADDITIONAL-TERMS.md` | OnAir, Rivet |
| `js8` | GPL-3.0-only | JS8Call |
| `mdc` | GPL-2.0-only | mdc-encode-decode, fsync-mdc1200-decode |
| `msumrcal` | GPL-3.0-only | SatDump |
| `multimon` | GPL-2.0-or-later | multimon-ng |
| `packet` | GPL-2.0-or-later | Dire Wolf |
| `rtl433` | GPL-2.0-or-later | rtl_433 |
| `sondes` | GPL-3.0-only | rs1729/RS |
| `stdc` | GPL-3.0 (version 3) | inmarsatc |
| `tetrapol` | GPL-2.0-only | tetrapol-kit-2023 |
| `trunkctl` | GPL-3.0-only | trunk-recorder, edacs-fm |
| `trunkfmt` | GPL-3.0-or-later | sdrtrunk |
| `weakmodes` | GPL-3.0-or-later | WSJT-X, wsjtx-ft2libre |
| `weakmodes2` | GPL-3.0-or-later with the additional terms in `ADDITIONAL-TERMS-OnAir.md` | OnAir, WSJT-X |

libfec (in `hfdl/src/libfec/`): the upstream project that `hfdl/UPSTREAM.md` names, dumphfdl at commit bb7234bdf12f, states about libfec only "libfec, (c) 2006 by Phil Karn, KA9Q" in its README and "May be used under the terms of the GNU Lesser General Public License (LGPL)" in the source headers (README read at https://github.com/szpajder/dumphfdl), and gives no LGPL version number. The text of version 2.1 is kept in `COPYING.LIB` so that an LGPL text is present; the version is the repository's choice, not upstream's statement.

Where an upstream states its licence only at project level or only in some files, this is what was read (2026-11-16, pages only, nothing downloaded):

- `fldigimodes`: https://github.com/w1hkj/fldigi shows "GPL-3.0 license" and a `COPYING` file. The Olivia/MFSK headers `include/jalocha/pj_*.h` carry no licence text and fldigi states no separate licence for them, so the folder is declared GPL-3.0-only.
- `tetrapol`: https://github.com/airphel/tetrapol-kit-2023: README "For license see COPYING"; `COPYING` is the GPL version 2 text. There are no per-file notices and no "or later" wording, so the folder is declared GPL-2.0-only.
The upstream URLs and commits are in each folder's `UPSTREAM.md`.


The changes made to each upstream project's files were made in November 2026. Each folder's `UPSTREAM.md` lists them.
