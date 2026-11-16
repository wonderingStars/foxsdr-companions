#include "csoundout.h"
#include "appglobal.h"

std::vector<short> txSamples48k;
int txFramesWritten = 0;

CSoundOut::CSoundOut()
{
}

CSoundOut::~CSoundOut()
{
}


//void CSoundOut::Init(int iNewBufferSize, bool bNewBlocking)
//{
//}

bool CSoundOut::Write(CVector< _SAMPLE >& psData)
{
  // psData holds interleaved stereo frames (left = real OFDM signal, right = 0, see CTransmitData with OF_REAL_VAL)
  const int frames = psData.Size() / 2;
  for (int i = 0; i < frames; i++) txSamples48k.push_back(psData[2 * i]);
  txFramesWritten++;
  return false;
}
