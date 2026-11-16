// rs_file.h - Reed-Solomon file layer of QSSTV (.rs1 .. .rs4 files), without Qt (hamdrm helper).
//
// Port of class reedSolomonCoder, src/utils/reedsolomoncoder.cpp and .h from QSSTV
// (https://github.com/ON4QZ/QSSTV, commit 31c323c), Copyright Johan Maes ON4QZ and the QSSTV authors, GNU GPL.
// It uses rs.cpp / rs.h (Phil Karn KA9Q's RS(255,k) codec) unchanged. Changes are listed in UPSTREAM.md:
// QByteArray/QString/QList became std::vector/std::string, the file output is returned in memory instead of
// being written by the caller's path, the erasure-position table is allocated large enough, and the surplus byte
// is read as unsigned.
#pragma once
#include <string>
#include <vector>

typedef std::vector<unsigned char> Bytes;

enum eRSType { RSTNONE, RST1, RST2, RST3, RST4 };

class reedSolomonCoder
{
public:
    reedSolomonCoder() {}
    // ba: the received file body (missing segments zero-filled); erasures: {totalSegments, segmentLength, missing segment numbers...}
    // origFileName: name as sent (e.g. "pic.rs2"); on success newFileName = base name + the 3-letter extension stored inside
    // the coded data, baFile = the decoded file. Returns false when the extension is not rs1..rs4, erasure info is missing,
    // or the code cannot correct the damage.
    bool decode(const Bytes &ba, const std::string &origFileName, std::string &newFileName, Bytes &baFile, const std::string &extension,
                const std::vector<int> &erasures);
    // encoder (used by the tests): ba in = file, out = rs coded body
    bool encode(Bytes &ba, const std::string &extension, eRSType rsType);

private:
    void distribute(const unsigned char *src, unsigned char *dst, int rows, int cols, int reverse);
    bool decode_and_write();
    Bytes ec_buf, tr_buf;
    int rs_bsize = 0, rs_dsize = 0, bep_size = 0;
    unsigned long sumOfFailures = 0, uncorrectableFailures = 0;
    eRSType fileType = RSTNONE;
    std::vector<int> newZeroPositions;
};
