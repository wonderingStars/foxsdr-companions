#ifndef DRMPARAMS_H
#define DRMPARAMS_H

#include <string>
#include <vector>

#define MINDRMSIZE 4000
#define MAXDRMSIZE 100000

struct drmTxParams
{
  int robMode;
  int qam;
  int bandwith;
  int interleaver;
  int protection;
  std::string callsign;
  int reedSolomon;
};



drmTxParams modeToParams(unsigned int mode);
unsigned int paramsToMode(drmTxParams prm);
extern int numTxFrames;
extern   drmTxParams drmParams;
extern std::vector<unsigned short> fixBlockList;


#endif // DRMPARAMS_H
