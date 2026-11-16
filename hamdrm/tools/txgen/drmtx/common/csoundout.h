#ifndef CSOUNDOUT_H
#define CSOUNDOUT_H
#include "GlobalDefinitions.h"
#include "soundinterface.h"
#include <vector>

// hamdrm_txgen: CSoundOut no longer talks to the QSSTV synthesiser/sound card. Write() appends the real (left)
// channel of every stereo frame it receives to txSamples48k (the 48 kHz stream QSSTV's chain produces) and counts
// the calls (one call = one 400 ms transmission frame). Both are defined in csoundout.cpp.
extern std::vector<short> txSamples48k;
extern int txFramesWritten;

class CSoundOut: public CSoundOutInterface
{
public:
  CSoundOut();
  ~CSoundOut();
//  void Init(int iNewBufferSize, bool bNewBlocking);
  bool Write(CVector< _SAMPLE >& psData);

};

#endif // CSOUNDOUT_H
