// SPDX-License-Identifier: GPL-3.0-only
//
// Part of the FoxSDR "js8" GPL helper (see ../UPSTREAM.md).
//
// Replaces upstream JS8.hpp and commons.h (JS8Call, GPL-3.0, (C) 2018 Jordan Sherer, (C) 2025 Allan Bazinet) with the
// parts the decoder needs and without Qt. Constants and the Costas arrays are copied from those two files.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>

// --- commons.h -----------------------------------------------------------------------------------------------------
#define JS8_NSPS           6192
#define JS8_NSMAX          6827
#define JS8_NTMAX          60
#define JS8_RX_SAMPLE_RATE 12000
#define JS8_RX_SAMPLE_SIZE (JS8_NTMAX * JS8_RX_SAMPLE_RATE)

#define JS8_NUM_SYMBOLS    79

#define JS8A_SYMBOL_SAMPLES 1920
#define JS8A_TX_SECONDS     15
#define JS8A_START_DELAY_MS 500

#define JS8B_SYMBOL_SAMPLES 1200
#define JS8B_TX_SECONDS     10
#define JS8B_START_DELAY_MS 200

#define JS8C_SYMBOL_SAMPLES 600
#define JS8C_TX_SECONDS     6
#define JS8C_START_DELAY_MS 100

#define JS8E_SYMBOL_SAMPLES 3840
#define JS8E_TX_SECONDS     30
#define JS8E_START_DELAY_MS 500

#define JS8I_SYMBOL_SAMPLES 384
#define JS8I_TX_SECONDS     4
#define JS8I_START_DELAY_MS 100

namespace JS8
{
  namespace Costas
  {
    // JS8 originally used the same Costas arrays as FT8 did, and so that's still the array in use by 'normal' mode.
    // All the other modes use the modified arrays.
    enum class Type
    {
      ORIGINAL,
      MODIFIED
    };

    using Array = std::array<std::array<int, 7>, 3>;

    inline constexpr Array ARRAYS[2] =
    {
      {{{4, 2, 5, 6, 1, 3, 0}, {4, 2, 5, 6, 1, 3, 0}, {4, 2, 5, 6, 1, 3, 0}}},
      {{{0, 6, 2, 3, 5, 4, 1}, {1, 5, 0, 2, 3, 6, 4}, {2, 5, 0, 6, 4, 1, 3}}}
    };

    constexpr Array const & array(Type type)
    {
      return ARRAYS[static_cast<int>(type)];
    }
  }

  // Submode ids as used by JS8Call (Varicode::SubmodeType); they are bits in the "submodes" set.
  enum Submode : int
  {
    NORMAL = 0, // "A", 15 s
    FAST   = 1, // "B", 10 s
    TURBO  = 2, // "C", 6 s
    SLOW   = 4, // "E", 30 s
    ULTRA  = 8  // "I", 4 s, present in the decoder source but disabled in the JS8Call build
  };

  // From the 12 characters of `message` (the 6-bit alphabet) and the 3-bit frame `type`, build the 79 tones.
  void
  encode(int                   type,
         Costas::Array const & costas,
         const char          * message,
         int                 * tones);

  namespace Event
  {
    struct Decoded
    {
      int         utc;       // period start, hhmmss (upstream code_time())
      int         snr;
      float       xdt;
      float       frequency;
      std::string data;      // the 12 characters of the 6-bit alphabet
      int         type;      // the 3 frame bits
      float       quality;
      int         mode;      // JS8::Submode
    };
  }

  // What a decode pass is told. The upstream `dec_data.params`, reduced to what a one-shot decode needs.
  struct DecodeParams
  {
    int nutc  = 0;     // period start, hhmmss
    int nfqso = 1500;  // preferred frequency: candidates near it are tried first
    int nfa   = 0;     // low decode limit, Hz
    int nfb   = 5000;  // high decode limit, Hz
  };

  using Emitter = std::function<void(Event::Decoded const &)>;

  // Decode one period of audio for one submode: `samples` are 12 kHz mono 16-bit, the start of the period first.
  // Returns the number of unique frames found. One decoder object per submode is built on first use (they are big).
  std::size_t decode(int                  submode,
                     DecodeParams const & params,
                     std::int16_t const * samples,
                     std::size_t          count,
                     Emitter const &      emit);

  // Constants of a submode.
  struct ModeInfo
  {
    int         submode;
    char        letter;
    const char *name;
    int         symbolSamples;
    int         periodSeconds;
    int         startDelayMS;
    Costas::Type costas;
    int         framesNeeded; // samples of a period needed to hold a whole frame
  };

  ModeInfo const * modeInfo(int submode);
}
