// rs_file.cpp - see rs_file.h. Port of QSSTV src/utils/reedsolomoncoder.cpp (GNU GPL). Upstream function names are kept.
#include "rs_file.h"
#include <algorithm>
#include <cstring>
#include "rs.h"

#define RSBSIZE 255
#define rse32 encode_rs
#define rsd32 eras_dec_rs
#define ENCODE 0
#define DECODE 1

static void leftJustified(Bytes &b, size_t n) // QByteArray::leftJustified(n, 0) (never truncates here)
{
    if (b.size() < n)
        b.resize(n, 0);
}

// reedSolomonCoder::decode
bool reedSolomonCoder::decode(const Bytes &ba, const std::string &origFileName, std::string &newFileName, Bytes &baFile, const std::string &extension,
                              const std::vector<int> &erasuresArray)
{
    // init()
    ec_buf.clear();
    tr_buf.clear();
    rs_bsize = rs_dsize = bep_size = 0;
    sumOfFailures = uncorrectableFailures = 0;
    newZeroPositions.clear();

    if (extension == "rs1")
        fileType = RST1;
    else if (extension == "rs2")
        fileType = RST2;
    else if (extension == "rs3")
        fileType = RST3;
    else if (extension == "rs4")
        fileType = RST4;
    else
        return false;
    tr_buf = ba;
    long got = (long)tr_buf.size();
    bep_size = (int)(got / RSBSIZE);
    if (got % RSBSIZE)
    {
        bep_size++;
        leftJustified(tr_buf, (size_t)bep_size * RSBSIZE);
    }
    if (bep_size <= 0 || bep_size > 100000)
        return false;
    int rest = (int)(tr_buf.size() % 64);
    if (rest != 0)
        leftJustified(tr_buf, (size_t)bep_size * RSBSIZE + (64 - rest));
    ec_buf.assign((size_t)bep_size * RSBSIZE, 0);

    rs_bsize = RSBSIZE;
    switch (fileType)
    {
    case RST1: rs_dsize = RSDSIZERS1; break;
    case RST2: rs_dsize = RSDSIZERS2; break;
    case RST3: rs_dsize = RSDSIZERS3; break;
    case RST4: rs_dsize = RSDSIZERS4; break;
    case RSTNONE: return false;
    }
    init_rs(rs_dsize);
    // setup erasure info
    if (erasuresArray.size() < 2)
        return false; // upstream: "no erasure info"
    const int totalSegments = erasuresArray[0];
    const int segmentLength = erasuresArray[1];
    const int numMissing = (int)erasuresArray.size() - 2;
    if (totalSegments <= 0 || segmentLength <= 0 || (long)totalSegments * segmentLength > 64L * 1024 * 1024)
        return false;
    // CHANGE: upstream allocates segmentLength*(totalSegments+1) entries but then reads up to 255*bep_size of them.
    std::vector<int> zeroPositions(std::max((size_t)segmentLength * ((size_t)totalSegments + 1), (size_t)RSBSIZE * bep_size), -1);
    newZeroPositions.assign((size_t)256 * bep_size, -1);
    /* now label the erasures positions */
    for (int i = 0; i < numMissing; i++)
    {
        if (erasuresArray[i + 2] < 0 || erasuresArray[i + 2] >= totalSegments)
            return false;
        long startOfSegment = (long)erasuresArray[i + 2] * segmentLength; // +2 because of header in array
        for (int j = 0; j < segmentLength; j++)
        {
            int row = (int)((startOfSegment + j) / bep_size);
            zeroPositions[startOfSegment + j] = row;
        }
    }
    /* distribute version pa0mbo for the indexes */
    int *pointzero = newZeroPositions.data();
    for (int i = 0; i < bep_size; i++)
        for (int j = 0; j < 255; j++)
            *(pointzero++) = zeroPositions[(size_t)j * bep_size + i];

    distribute(tr_buf.data(), ec_buf.data(), bep_size, rs_bsize, DECODE);
    if (!decode_and_write())
        return false;
    if (uncorrectableFailures > 0)
        return false;
    if (tr_buf.size() < 7)
        return false;
    if (bep_size != (int)(tr_buf[1] + tr_buf[2] * 256))
        return false; // upstream: "problems with bep_size coded in file"
    // CHANGE: upstream reads tr_buf[0] as a signed char; the encoder stores an unsigned value up to 224.
    int coded_file_size = bep_size * rs_dsize - (int)tr_buf[0];
    char coded_file_ext[4];
    memcpy(coded_file_ext, tr_buf.data() + 3, 3);
    coded_file_ext[3] = 0;

    // QFileInfo::completeBaseName() of the sent name (everything before the last dot)
    std::string baseName = origFileName;
    size_t dot = baseName.rfind('.');
    if (dot != std::string::npos)
        baseName.resize(dot);
    baseName += ".";
    // CHANGE: keep only letters and digits of the stored extension (it comes from the air)
    for (int i = 0; i < 3; i++)
        if ((coded_file_ext[i] >= '0' && coded_file_ext[i] <= '9') || (coded_file_ext[i] >= 'A' && coded_file_ext[i] <= 'Z') || (coded_file_ext[i] >= 'a' && coded_file_ext[i] <= 'z'))
            baseName += coded_file_ext[i];

    Bytes body(tr_buf.begin() + 7, tr_buf.end()); // tr_buf.right(count-7)
    if (coded_file_size < 0)
        return false;
    if ((size_t)coded_file_size < body.size())
        body.resize(coded_file_size); // .left(coded_file_size)
    newFileName = baseName;
    baFile = body;
    return true;
}

// reedSolomonCoder::distribute
void reedSolomonCoder::distribute(const unsigned char *src, unsigned char *dst, int rows, int cols, int reverse)
{
    unsigned int i, j, rc, ri, rl;
    rc = rows * cols;
    ri = 0;
    rl = reverse ? cols : rows;
    for (i = 0; i < rc; i += 64)
    {
        for (j = 0; j < 64 && i + j < rc; j++)
        {
            *(dst + ri) = *(src + i + j);
            ri += rl; /* next position in dst */
            if (ri >= rc)
                ri -= rc - 1; /* go around and add one. */
        }
    }
}

// reedSolomonCoder::decode_and_write
bool reedSolomonCoder::decode_and_write()
{
    int nr_erasures;
    int eras_pos[255];
    tr_buf.clear();
    for (int i = 0; i < bep_size; i++)
    {
        /* get the erasure positions and their number */
        nr_erasures = 0;
        for (int j = 0; j < rs_bsize; j++)
        {
            if (newZeroPositions[(size_t)i * rs_bsize + j] != -1)
            {
                eras_pos[nr_erasures++] = newZeroPositions[(size_t)i * rs_bsize + j];
                if (nr_erasures > (rs_bsize - rs_dsize))
                    return false;
            }
        }
        if (nr_erasures > (rs_bsize - rs_dsize))
            nr_erasures = rs_bsize - rs_dsize - 1;
        int failure = rsd32(ec_buf.data() + (size_t)i * rs_bsize, eras_pos, nr_erasures);
        if (failure > 0)
            sumOfFailures += failure;
        else if (failure == -1)
        {
            uncorrectableFailures++;
            return false;
        }
        tr_buf.insert(tr_buf.end(), ec_buf.begin() + (size_t)i * rs_bsize, ec_buf.begin() + (size_t)i * rs_bsize + rs_dsize);
    }
    return true;
}

// reedSolomonCoder::encode
bool reedSolomonCoder::encode(Bytes &ba, const std::string &extension, eRSType rsType)
{
    std::string suffix = extension;
    suffix.resize(3, '\0'); // leftJustified(3, 0)
    tr_buf = ba;
    fileType = rsType;
    rs_bsize = RSBSIZE;
    switch (fileType)
    {
    case RST1: rs_dsize = RSDSIZERS1; break;
    case RST2: rs_dsize = RSDSIZERS2; break;
    case RST3: rs_dsize = RSDSIZERS3; break;
    case RST4: rs_dsize = RSDSIZERS4; break;
    case RSTNONE: return false;
    }
    init_rs(rs_dsize);
    long got = (long)tr_buf.size();
    long chunks = (got + 7) / rs_dsize;
    if (((got + 7) % rs_dsize) > 0)
        chunks++;
    bep_size = (int)chunks;
    ec_buf.clear();
    unsigned char dataByte = (unsigned char)(rs_dsize - (got % rs_dsize)); /* surplus in filelength */
    ec_buf.push_back(dataByte);
    ec_buf.push_back((unsigned char)(chunks % 256));
    ec_buf.push_back((unsigned char)(chunks / 256));
    ec_buf.push_back((unsigned char)suffix[0]);
    ec_buf.push_back((unsigned char)suffix[1]);
    ec_buf.push_back((unsigned char)suffix[2]);
    ec_buf.push_back(0);
    {
        size_t n = std::min((size_t)(rs_dsize - 7), tr_buf.size());
        ec_buf.insert(ec_buf.end(), tr_buf.begin(), tr_buf.begin() + n);
    }
    ec_buf.resize(ec_buf.size() + RSBSIZE - rs_dsize);
    rse32(ec_buf.data(), ec_buf.data() + rs_dsize);
    for (int i = 1; i < bep_size; i++)
    {
        long from = (long)i * rs_dsize - 7;
        Bytes temp;
        if (from < (long)tr_buf.size())
            temp.assign(tr_buf.begin() + from, tr_buf.begin() + std::min((size_t)(from + rs_dsize), tr_buf.size()));
        if (temp.empty())
            break;
        ec_buf.insert(ec_buf.end(), temp.begin(), temp.end());
        if ((int)temp.size() < rs_dsize)
            ec_buf.insert(ec_buf.end(), rs_dsize - temp.size(), 0);
        ec_buf.resize(ec_buf.size() + RSBSIZE - rs_dsize);
        rse32(ec_buf.data() + (size_t)i * rs_bsize, ec_buf.data() + (size_t)i * rs_bsize + rs_dsize);
    }
    ba.assign(ec_buf.size(), 0);
    distribute(ec_buf.data(), ba.data(), bep_size, rs_bsize, ENCODE);
    return true;
}
