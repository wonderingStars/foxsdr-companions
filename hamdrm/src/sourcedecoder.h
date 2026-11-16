// sourcedecoder.h - HamDRM data packet and MOT file reassembly, without Qt (hamdrm helper).
//
// Rewrite of class sourceDecoder and its structures from QSSTV src/drmrx/sourcedecoder.h / .cpp
// (https://github.com/ON4QZ/QSSTV, commit 31c323c), Copyright (C) 2000-2019 Johan Maes ON4QZ, GNU General Public
// License version 2 or (at your option) any later version. The decoding logic (data block -> data packet -> MOT
// header / body segments -> transport blocks -> file) is kept; Qt types became std types, every read is bounds
// checked, and the GUI events became a callback (RxEvent). Everything that is not receiving is left out:
// hybrid (FTP) downloads, BSR / FIX repair requests, image display, the log book. See UPSTREAM.md.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#define PACKETBUFFERLEN 512
#define MAXTRANSPORTLISTS 5

typedef std::vector<unsigned char> ByteVec;

enum edataGroupType { GENDATA, CAMESS, GENCA, MOTHEAD, MOTDATA, MOTDATACA };

struct dataSegment
{
    dataSegment(int newSize)
    {
        crcOK = false;
        recovered = false;
        segmentNumber = -1;
        data.assign(newSize > 0 ? (size_t)newSize : 0, 0xAA);
    }
    void setData(const ByteVec &ba, short int segNumber, bool crcok)
    {
        crcOK = crcok;
        data = ba;
        segmentNumber = segNumber;
    }
    void clearData() { data.clear(); }
    bool crcOK;
    bool recovered;
    short int segmentNumber;
    bool hasData() const { return crcOK || recovered; }
    ByteVec data;
};

struct dataPacket
{
    ByteVec ba;
    bool extFlag = false, crcFlag = false, sessionFlag = false, userFlag = false, crcOK = false;
    edataGroupType dataGroupType = GENDATA;
    unsigned char continuityIndex = 0, repetitionIndex = 0;
    unsigned short segmentNumber = 0;
    bool lastSegment = false;
    unsigned short transportID = 0xFFFF;
    unsigned char userAccessField = 0;
    unsigned short segmentSize = 0;
    int offset = 0;
    int lenght = 0;
    size_t advance(size_t numBytes)
    {
        ba.erase(ba.begin(), ba.begin() + (numBytes < ba.size() ? numBytes : ba.size()));
        return ba.size();
    }
    size_t chop(size_t numBytes)
    {
        ba.resize(ba.size() - (numBytes < ba.size() ? numBytes : ba.size()));
        return ba.size();
    }
};

struct dataBlock
{
    bool firstFlag = false, lastFlag = false;
    short packetID = 0;
    bool PPI = false;
    unsigned char continuityIndex = 0;
    bool crcOK = false;
    unsigned int length = 0;
    ByteVec ba;
};

struct transportBlock
{
    transportBlock(unsigned short tId)
    {
        clear();
        transportID = tId;
    }
    void clear()
    {
        totalSegments = 0;
        headerReceived = false;
        segmentsReceived = 0;
        bodySize = 0;
        headerSize = 0;
        alreadyReceived = false;
        retrieveTries = 0;
        lastSegmentReceived = false;
        defaultSegmentSize = 0;
        contentType = contentSubtype = 0;
        robMode = interLeaver = mscMode = mpx = spectrum = 0;
        modeCode = 0;
    }
    bool isComplete() const
    {
        if (!headerReceived)
            return false;
        if (segmentsReceived < totalSegments)
            return false;
        return true;
    }
    int isAlmostComplete() const
    {
        if (!headerReceived)
            return 0;
        if (totalSegments == 0)
            return 0;
        return (segmentsReceived * 100) / totalSegments;
    }
    void setAlreadyReceived(bool aRx)
    {
        if (aRx)
        {
            alreadyReceived = true;
            for (auto &s : dataSegmentList)
                s.clearData();
        }
    }
    unsigned short transportID;
    unsigned int bodySize;
    unsigned int headerSize;
    unsigned short contentType;
    unsigned short contentSubtype;
    unsigned int defaultSegmentSize;
    std::string fileName;
    std::string newFileName;
    bool headerReceived;
    bool alreadyReceived;
    int retrieveTries;
    int segmentsReceived;
    bool lastSegmentReceived;
    int totalSegments;
    std::string callsign;
    std::vector<dataSegment> dataSegmentList;
    int robMode, interLeaver, mscMode, mpx, spectrum;
    unsigned int modeCode; // mode(A=0,B=1,E=2) BW(0=2.3,1=2.5) prot(High=0,LOW=1) QAM(4=0,16=1,64=2) interleaver
};

// What the receiver reports (replaces the GUI events of QSSTV)
struct RxEvent
{
    enum Kind { Header, Segment, File, Skipped } kind = Header;
    unsigned transportId = 0;
    std::string name;        // file name as sent (Header, Segment, File)
    unsigned bodySize = 0;   // from the MOT header (Header)
    int segment = -1;        // Segment: segment number
    int received = 0;        // segments received so far
    int total = 0;           // segments known so far
    bool lastKnown = false;  // the last segment has been seen
    std::string savedName;   // File: name written in the output folder ("" when not written)
    std::string savedPath;   // File: path written ("" when --out-dir is off)
    size_t size = 0;         // File: size in bytes
    std::string sha256;      // File: sha256 of the bytes
    std::string fec;         // File: "rs1".."rs4" when Reed-Solomon decoded, else ""
    std::string mode;        // e.g. "B/2.3/Hi/16/Short"
    std::string callsign;
    std::string note;        // Skipped / File: why nothing was written ("hybrid", "bsr", "too large", "write failed")
    double snr_db = 0;
};

std::string compactModeToString(unsigned mode); // port of compactModeToString (drmstatusframe.cpp)

class sourceDecoder
{
public:
    sourceDecoder();
    void init();
    bool decode();
    void setOutDir(const std::string &d) { outDir = d; }
    void setSink(std::function<void(const RxEvent &)> f) { sink = f; }
    // counters for the stats event
    unsigned long statPackets = 0, statCrcFail = 0, statDropped = 0;

private:
    bool setupDataBlock(unsigned char *buffer, bool crcIsOK, int len);
    bool setupDataPacket(const ByteVec &ba);
    void addDataSegment();
    bool addHeaderSegment();
    void loadParams(transportBlock *tbPtr, unsigned char paramID, const unsigned char *data, int len);
    void writeData(transportBlock *tbPtr);
    bool checkSaveImage(const ByteVec &ba, transportBlock *tbPtr);
    void saveImage(transportBlock *tbPtr);
    transportBlock *getTransporPtr(unsigned short tId, bool create);
    bool writeFile(transportBlock *tb, const std::string &name, const ByteVec &bytes, std::string &savedName, std::string &savedPath);
    unsigned char packetBuffer[PACKETBUFFERLEN];
    dataBlock currentDataBlock;
    dataPacket currentDataPacket;
    std::vector<transportBlock *> transportBlockPtrList;
    transportBlock *lastTransportBlockPtr = nullptr;
    std::vector<int> erasureList;
    ByteVec holdingBuffer;
    short int lastContinuityIndex = -1;
    bool checkIt = false;
    bool alreadyDisplayed = false;
    unsigned modeCodeTmp = 0;
    std::string callsignTmp;
    std::string outDir;
    std::string pendingFec;
    std::function<void(const RxEvent &)> sink;
};
