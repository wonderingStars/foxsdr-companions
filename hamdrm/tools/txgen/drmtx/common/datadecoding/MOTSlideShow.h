/******************************************************************************\
 * Technische Universitaet Darmstadt, Institut fuer Nachrichtentechnik
 * Copyright (c) 2001
 *
 * Author(s):
 *	Volker Fischer
 *
 * Description:
 *
 ******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
\******************************************************************************/

#if !defined(MOTSLIDESHOW_H__3B0UBVE98732KJVEW363LIHGEW982__INCLUDED_)
#define MOTSLIDESHOW_H__3B0UBVE98732KJVEW363LIHGEW982__INCLUDED_

#include "../GlobalDefinitions.h"
#include "vector.h"
#include "DABMOT.h"
#include "../Parameter.h"


/* Classes ********************************************************************/
/* Encoder ------------------------------------------------------------------ */
class CMOTSlideShowEncoder
{
public:
  CMOTSlideShowEncoder ():vecPicFileNames (0) {strCurObjName="";}
  virtual ~ CMOTSlideShowEncoder () {}
  void Init (CParameter &TParam);
  void GetDataUnit(CVector < _BINARY > &vecbiNewData);
  void AddArray (std::vector<unsigned char> *ba, const std::string name, const std::string format);
  void ClearAllFileNames () {vecPicFileNames.Init (0);}
  _BOOLEAN GetTransStat (string & strCurPict, _REAL & rCurPerc) const;

protected:
  struct SPicDescr
  {
    std::vector<unsigned char> *arrayPtr;
    std::string name;
    std::string format;
  };
  void AddNextPicture ();
  CMOTDABEnc MOTDAB;
  CVector < SPicDescr > vecPicFileNames;
  int iPictureCnt;
  string strCurObjName;
  bool allDataSend;
  int extraBlocks;
  int sliceCounter;
  int bytesToBeUsed;
};

#endif // !defined(MOTSLIDESHOW_H__3B0UBVE98732KJVEW363LIHGEW982__INCLUDED_)
