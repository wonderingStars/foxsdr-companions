// sourcedecoder.cpp - see sourcedecoder.h. Rewrite, without Qt, of QSSTV src/drmrx/sourcedecoder.cpp
// Copyright (C) 2000-2019 Johan Maes ON4QZ (on4qz@telenet.be), GNU General Public License version 2 or (at your option) any later version.
// Upstream function names are kept in the comments; every change is listed in UPSTREAM.md.
#include "sourcedecoder.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

#include "demodulator.h"
#include "drm.h"
#include "drmproto.h"
#include "rs_file.h"
#include "sha256.h"

static const size_t kMaxTotalBytes = 32u * 1024 * 1024; // CHANGE: bound on the memory one file may take
static const unsigned kMaxBodySize = 16u * 1024 * 1024; // CHANGE: files larger than this are not assembled

sourceDecoder::sourceDecoder() { transportBlockPtrList.clear(); }

// sourceDecoder::init
void sourceDecoder::init()
{
    for (auto *t : transportBlockPtrList)
        delete t;
    transportBlockPtrList.clear();
    lastTransportBlockPtr = nullptr;
    bodyTotalSegments = 0;
    checkIt = false;
    erasureList.clear();
    lastContinuityIndex = -1;
    alreadyDisplayed = false;
    holdingBuffer.clear();
}

/*
 sourceDecoder::decode: decode of a valid data block
 - header 8 bits, data field n bytes, CRC 16 bits
*/
bool sourceDecoder::decode()
{
    double checksum;
    int N_partB;
    if (channel_decoded_data_buffer_data_valid != 1)
        return false;
    if (audio_data_flag == 0)
        return false; // "audio decoding not implemented in qsstv"
    N_partB = (int)(length_decoded_data / 8);
    if (N_partB > PACKETBUFFERLEN || N_partB < 3)
    {
        // CHANGE: upstream only logs "packet buffer length exceeded" and then overruns packetBuffer
        statDropped++;
        msc_valid = INVALID;
        return false;
    }
    bits2bytes(channel_decoded_data_buffer, N_partB * 8, packetBuffer);
    crc16_bytewise(&checksum, packetBuffer, N_partB);
    if (fabs(checksum) <= DBL_EPSILON)
    {
        statPackets++;
        if (!setupDataBlock(packetBuffer, true, N_partB))
        {
            msc_valid = INVALID;
            return false;
        }
    }
    else
    {
        statCrcFail++;
        msc_valid = INVALID;
        return false;
    }
    // at this point we have a dataPacket we now check header / data and build a transport stream
    switch (currentDataPacket.dataGroupType)
    {
    case MOTDATA:
        addDataSegment();
        break;
    case MOTHEAD:
        addHeaderSegment();
        break;
    default:
        return false;
    }
    return true;
}

// sourceDecoder::setupDataBlock
bool sourceDecoder::setupDataBlock(unsigned char *buffer, bool crcIsOK, int len)
{
    currentDataBlock.length = len;
    unsigned char header = buffer[0];
    int availableBytes;
    const unsigned char *bufPtr;
    currentDataBlock.ba.assign(buffer, buffer + len);
    currentDataBlock.crcOK = crcIsOK; // CHANGE: upstream never sets crcOK here (it is only logged and copied to the packet)
    currentDataBlock.firstFlag = currentDataBlock.lastFlag = false;
    if (header & 0x80)
        currentDataBlock.firstFlag = true;
    if (header & 0x40)
        currentDataBlock.lastFlag = true;
    currentDataBlock.packetID = (header & 0x30) >> 4;
    currentDataBlock.PPI = (header & 0x8) >> 3;
    currentDataBlock.continuityIndex = (header & 0x7);
    if ((currentDataBlock.PPI != 0) && (crcIsOK))
    {
        availableBytes = buffer[1];
        bufPtr = &buffer[2];
        if (availableBytes > len - 4) // CHANGE: bound the padding indicator (upstream trusts it)
            return false;
    }
    else
    {
        availableBytes = len - 3;
        bufPtr = &buffer[1];
    }
    if (currentDataBlock.firstFlag)
    {
        holdingBuffer.clear();
        lastContinuityIndex = currentDataBlock.continuityIndex;
    }
    else
    {
        if (lastContinuityIndex < 0)
            return false;
        lastContinuityIndex = (lastContinuityIndex + 1) % 8;
        if (currentDataBlock.continuityIndex != lastContinuityIndex)
        {
            lastContinuityIndex = -1;
            return false;
        }
    }
    if (availableBytes < 0)
        return false;
    holdingBuffer.insert(holdingBuffer.end(), bufPtr, bufPtr + availableBytes);
    if (holdingBuffer.size() > 1024u * 1024u) // CHANGE: bound the data group size
    {
        holdingBuffer.clear();
        lastContinuityIndex = -1;
        return false;
    }
    if (currentDataBlock.lastFlag)
        return setupDataPacket(holdingBuffer);
    return false;
}

//MSC Header Description 2 bytes or 4 bytes if Extension field
// First Byte: B7 Extension Flag, B6 CRC Flag, B5 Session Flag, B4 UserAccess Flag, B3-B0 Data Group Type
//    0 General data; 1 CA messages; 2 General data and CA parameters; 3 MOT header information; 4 MOT data; 5 MOT data and CA parameters.
// Second Byte: B7-B4 Continuity Index, B3-B0 Repetition Index. 2 more extension bytes if the flag is set.
// Session header (if session flag): B15 last segment, B14-B0 segment number.
// User access field: B7-B5 rfa, B4 TransportID present flag, B3-B0 length of the user access fields; then TransportID (16 bits).
// Then a 2 bytes segmentation header: B15-B13 repetition count (not used), B12-B0 length of the data that follows.
bool sourceDecoder::setupDataPacket(const ByteVec &ba)
{
    double checksum;
    unsigned char lengthIndicator;
    unsigned char header;

    if (ba.size() < 2)
        return false;
    currentDataPacket.ba = ba;
    header = ba[0];
    currentDataPacket.transportID = 0xFFFF;
    currentDataPacket.extFlag = false;
    currentDataPacket.crcFlag = false;
    currentDataPacket.sessionFlag = false;
    currentDataPacket.userFlag = false;
    currentDataPacket.lastSegment = false;
    currentDataPacket.segmentNumber = 0; // CHANGE: upstream keeps the previous packet's value
    currentDataPacket.crcOK = currentDataBlock.crcOK;

    if (header & 0x10)
        currentDataPacket.userFlag = true;
    currentDataPacket.dataGroupType = (edataGroupType)(header & 0x07);
    if (header & 0x80)
        currentDataPacket.extFlag = true;
    if (header & 0x20)
        currentDataPacket.sessionFlag = true;

    if (header & 0x40)
    {
        currentDataPacket.crcFlag = true;
        crc16_bytewise(&checksum, (unsigned char *)currentDataPacket.ba.data(), (long)currentDataPacket.ba.size());
        if (fabs(checksum) <= DBL_EPSILON)
            currentDataPacket.crcOK = true;
        else
        {
            currentDataPacket.crcOK = false;
            msc_valid = INVALID;
            return false;
        }
        currentDataPacket.chop(2); // drop crc
    }
    currentDataPacket.advance(2); // skip header and continuity bits
    if (currentDataPacket.extFlag)
        currentDataPacket.advance(2); // just skip the extension bytes

    if (currentDataPacket.sessionFlag)
    {
        if (currentDataPacket.ba.size() < 2)
            return false;
        currentDataPacket.segmentNumber = (unsigned short)((currentDataPacket.ba[0] & 0x7F) * 256 + currentDataPacket.ba[1]);
        if (currentDataPacket.ba[0] & 0x80)
            currentDataPacket.lastSegment = true;
        currentSegmentNumber = currentDataPacket.segmentNumber;
        currentDataPacket.advance(2);
    }

    if (currentDataPacket.userFlag)
    {
        if (currentDataPacket.ba.size() < 1)
            return false;
        currentDataPacket.userAccessField = currentDataPacket.ba[0];
        currentDataPacket.advance(1);
        lengthIndicator = (currentDataPacket.userAccessField & 0xF);
        if ((currentDataPacket.userAccessField & 0x10) && (lengthIndicator >= 2))
        {
            if (currentDataPacket.ba.size() < 2)
                return false;
            currentDataPacket.transportID = (unsigned short)(currentDataPacket.ba[0] * 256 + currentDataPacket.ba[1]);
        }
        currentDataPacket.advance(lengthIndicator);
    }
    if (currentDataPacket.ba.size() < 2)
        return false;
    currentDataPacket.segmentSize = (unsigned short)((currentDataPacket.ba[0] & 0x1F) * 256 + currentDataPacket.ba[1]);
    currentDataPacket.advance(2);
    currentDataPacket.lenght = (int)currentDataPacket.ba.size();
    return true;
}

// sourceDecoder::addHeaderSegment
bool sourceDecoder::addHeaderSegment()
{
    transportBlock *tbPtr = getTransporPtr(currentDataPacket.transportID, true);
    if (!tbPtr->alreadyReceived)
        msc_valid = VALID;
    else
    {
        msc_valid = ALREADYRECEIVED;
        alreadyDisplayed = true; // upstream re-displays the finished image here; there is nothing to display
        return true;
    }
    const ByteVec &d = currentDataPacket.ba;
    if (d.size() < 7)
        return false; // CHANGE: upstream reads 7 bytes unchecked
    const bool first = !tbPtr->headerReceived;
    tbPtr->headerReceived = true;
    tbPtr->bodySize = (((unsigned int)d[0]) << 20) + (((unsigned int)d[1]) << 12) + (((unsigned int)d[2]) << 4) + ((((unsigned int)d[3]) & 0xF0) >> 4);
    tbPtr->headerSize = (((unsigned int)d[3] & 0x0F) << 9) + (((unsigned int)d[4]) << 1) + ((((unsigned int)d[5]) & 0x80) >> 7);
    tbPtr->contentType = (unsigned short)(((unsigned int)d[5] & 0x7E) >> 1);
    tbPtr->contentSubtype = (unsigned short)(((((unsigned int)d[5]) & 0x1) << 8) + ((unsigned int)d[6]));

    // The header core is followed by a number of parameter blocks; the first byte of every parameter block
    // contains a 2-bit PLI (B7 and B6) indicating the type of parameter block.
    size_t pos = 7;
    while (pos < d.size())
    {
        unsigned char PLI = d[pos] >> 6;
        unsigned char paramID = d[pos] & 0x3F;
        switch (PLI)
        {
        case 0:
            pos += 1;
            break;
        case 1:
            loadParams(tbPtr, paramID, &d[pos], 1);
            pos += 2;
            break;
        case 2:
            loadParams(tbPtr, paramID, &d[pos], 4);
            pos += 5;
            break;
        default:
        {
            unsigned char extBit = d[pos] & 0x80;
            size_t dataFieldLength;
            if (extBit)
            {
                if (pos + 1 >= d.size())
                    return true;
                dataFieldLength = (size_t)256 * (d[pos] & 0x7F) + d[pos + 1];
                pos += 2;
            }
            else
            {
                dataFieldLength = d[pos] & 0x7F;
                pos += 1;
            }
            if (pos + dataFieldLength > d.size())
                dataFieldLength = d.size() - pos; // CHANGE: clamp (upstream advances past the end)
            loadParams(tbPtr, paramID, &d[pos], (int)dataFieldLength);
            pos += dataFieldLength;
            break;
        }
        }
    }
    if (first && sink)
    {
        RxEvent ev;
        ev.kind = RxEvent::Header;
        ev.transportId = tbPtr->transportID;
        ev.name = tbPtr->fileName;
        ev.bodySize = tbPtr->bodySize;
        ev.mode = compactModeToString(tbPtr->modeCode);
        ev.callsign = tbPtr->callsign;
        ev.snr_db = avgSNR;
        sink(ev);
    }
    return true;
}

// sourceDecoder::loadParams (only the content name, parameter 12, is used)
void sourceDecoder::loadParams(transportBlock *tbPtr, unsigned char paramID, const unsigned char *data, int len)
{
    switch (paramID)
    {
    case 5: // expiration
        break;
    case 6:
        break;
    case 12: // content name: one character-set byte, then the name (upstream: fromLatin1(data()+1).left(len-1))
    {
        std::string s;
        for (int i = 1; i < len && data[i] != 0; i++)
            s += (char)data[i];
        tbPtr->fileName = s;
        break;
    }
    default:
        break;
    }
}

// sourceDecoder::addDataSegment
void sourceDecoder::addDataSegment()
{
    transportBlock *tbPtr = getTransporPtr(currentDataPacket.transportID, true);
    rxTransportID = currentDataPacket.transportID;
    if (callsignValid)
        tbPtr->callsign = drmCallsign;

    if (!tbPtr->alreadyReceived)
        msc_valid = VALID;
    else
    {
        msc_valid = ALREADYRECEIVED;
        checkIt = false;
        return; // CHANGE: upstream keeps filling a finished transport block and clears it again
    }
    if (tbPtr->headerReceived && tbPtr->bodySize > kMaxBodySize)
    {
        statDropped++;
        return;
    }
    const unsigned segNo = currentDataPacket.segmentNumber;
    const size_t segSize = currentDataPacket.segmentSize > tbPtr->defaultSegmentSize ? currentDataPacket.segmentSize : tbPtr->defaultSegmentSize;
    if ((size_t)(segNo + 1) * (segSize ? segSize : 1) > kMaxTotalBytes)
    {
        statDropped++; // CHANGE: bound the memory a single file may take
        return;
    }
    if (currentDataPacket.lastSegment)
    {
        tbPtr->totalSegments = currentDataPacket.segmentNumber + 1;
        tbPtr->lastSegmentReceived = true;
    }
    else
        tbPtr->defaultSegmentSize = currentDataPacket.segmentSize;
    while (tbPtr->dataSegmentList.size() <= segNo)
        tbPtr->dataSegmentList.emplace_back((int)tbPtr->defaultSegmentSize);

    const bool isNew = !tbPtr->dataSegmentList[segNo].hasData();
    if (isNew)
        checkIt = true;
    else
        msc_valid = ALREADYRECEIVED;
    if (tbPtr->totalSegments < (int)segNo + 1)
        tbPtr->totalSegments = (int)segNo + 1;
    bodyTotalSegments = tbPtr->totalSegments;
    rxSegments = tbPtr->segmentsReceived;

    tbPtr->dataSegmentList[segNo].setData(currentDataPacket.ba, (short)segNo, true);

    writeData(tbPtr);

    if (isNew && sink)
    {
        RxEvent ev;
        ev.kind = RxEvent::Segment;
        ev.transportId = tbPtr->transportID;
        ev.name = tbPtr->fileName;
        ev.segment = (int)segNo;
        ev.received = tbPtr->segmentsReceived;
        ev.total = tbPtr->totalSegments;
        ev.lastKnown = tbPtr->lastSegmentReceived;
        ev.mode = compactModeToString(tbPtr->modeCode);
        ev.snr_db = avgSNR;
        sink(ev);
    }
}

// sourceDecoder::writeData
void sourceDecoder::writeData(transportBlock *tbPtr)
{
    ByteVec ba;
    erasureList.clear();
    erasureList.push_back(tbPtr->totalSegments);
    erasureList.push_back((int)tbPtr->defaultSegmentSize);
    for (size_t i = 0; i < tbPtr->dataSegmentList.size(); i++)
    {
        if (!tbPtr->dataSegmentList[i].hasData())
        {
            erasureList.push_back((int)i);
            ba.insert(ba.end(), tbPtr->defaultSegmentSize, 0x00);
        }
        else
            ba.insert(ba.end(), tbPtr->dataSegmentList[i].data.begin(), tbPtr->dataSegmentList[i].data.end());
    }
    tbPtr->segmentsReceived = 0;
    drmBlockList.clear();
    for (size_t i = 0; i < tbPtr->dataSegmentList.size(); i++)
    {
        if (tbPtr->dataSegmentList[i].hasData())
        {
            drmBlockList.push_back((unsigned short)i);
            tbPtr->segmentsReceived++;
        }
    }
    if (tbPtr->isAlmostComplete() < 63)
        return;
    if (!tbPtr->lastSegmentReceived)
        return;
    checkSaveImage(ba, tbPtr);
}

// sourceDecoder::saveImage (the hybrid / FTP branch of upstream is reported, not executed)
void sourceDecoder::saveImage(transportBlock *tbPtr)
{
    modeCodeTmp = tbPtr->modeCode;
    callsignTmp = tbPtr->callsign;
    alreadyDisplayed = true;
    if (tbPtr->alreadyReceived)
        return;
    if (tbPtr->fileName.empty())
        return;
    if (tbPtr->retrieveTries == 0)
        lastAvgSNR = avgSNR;
    tbPtr->setAlreadyReceived(true);
}

static std::string lowerExt(const std::string &fn)
{
    size_t dot = fn.rfind('.');
    std::string e = dot == std::string::npos ? "" : fn.substr(dot + 1);
    for (auto &c : e)
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
    return e;
}

// A name that came over the air becomes a plain file name: no folders, no characters Windows refuses, no device names.
static std::string safeName(const std::string &in, unsigned tid)
{
    std::string base = in;
    size_t cut = base.find_last_of("/\\:");
    if (cut != std::string::npos)
        base = base.substr(cut + 1);
    std::string out;
    for (unsigned char c : base)
    {
        if (c < 0x20 || c == 0x7f || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            continue;
        out += (char)c;
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    while (!out.empty() && out.front() == '.')
        out.erase(out.begin()); // no hidden or "." / ".." names
    if (out.size() > 100)
        out.resize(100);
    std::string stem = out.substr(0, out.find('.'));
    for (auto &c : stem)
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
    static const std::set<std::string> reserved = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                                    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (out.empty() || reserved.count(stem))
        out = "file" + std::to_string(tid) + (out.empty() ? ".bin" : "_" + out);
    return out;
}

// Writes bytes to outDir/name (a new name if it exists: name_1.ext ...). Returns false when --out-dir is off or the write failed.
bool sourceDecoder::writeFile(transportBlock *tb, const std::string &name, const ByteVec &bytes, std::string &savedName, std::string &savedPath)
{
    savedName.clear();
    savedPath.clear();
    if (outDir.empty())
        return false;
    std::string base = safeName(name, tb->transportID);
    std::string stem = base, ext;
    size_t dot = base.rfind('.');
    if (dot != std::string::npos && dot > 0)
    {
        stem = base.substr(0, dot);
        ext = base.substr(dot);
    }
    std::string dir = outDir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        dir += '/';
    std::string cand = base;
    for (int n = 1; n < 10000; n++)
    {
        std::string path = dir + cand;
        FILE *t = fopen(path.c_str(), "rb");
        if (!t)
        {
            FILE *f = fopen(path.c_str(), "wb");
            if (!f)
                return false;
            size_t w = bytes.empty() ? 0 : fwrite(bytes.data(), 1, bytes.size(), f);
            int rc = fclose(f);
            if (w != bytes.size() || rc != 0)
                return false;
            savedName = cand;
            savedPath = path;
            return true;
        }
        fclose(t);
        cand = stem + "_" + std::to_string(n) + ext;
    }
    return false;
}

// sourceDecoder::checkSaveImage (the QSSTV GUI hand-offs are replaced by the File event)
bool sourceDecoder::checkSaveImage(const ByteVec &ba, transportBlock *tbPtr)
{
    reedSolomonCoder rsd;
    std::string extension;
    ByteVec baFile;
    const ByteVec *baFilePtr;
    std::string fec;

    if (!checkIt)
        return false;
    extension = lowerExt(tbPtr->fileName);
    tbPtr->newFileName = tbPtr->fileName;
    if ((extension == "rs1") || (extension == "rs2") || (extension == "rs3") || (extension == "rs4"))
    {
        // try to decode
        if (tbPtr->alreadyReceived)
            return false;
        std::string decodedName;
        if (!rsd.decode(ba, tbPtr->fileName, decodedName, baFile, extension, erasureList))
            return false;
        tbPtr->newFileName = decodedName;
        baFilePtr = &baFile;
        fec = extension;
    }
    else
    {
        if (!tbPtr->isComplete())
            return false;
        if (tbPtr->fileName == "bsr.bin" && !tbPtr->alreadyReceived)
        {
            // a repair request from a transmitter (QSSTV hands it to its transmit side); nothing to do when only receiving
            tbPtr->setAlreadyReceived(true);
            if (sink)
            {
                RxEvent ev;
                ev.kind = RxEvent::Skipped;
                ev.transportId = tbPtr->transportID;
                ev.name = tbPtr->fileName;
                ev.note = "bsr";
                sink(ev);
            }
            return false;
        }
        baFilePtr = &ba;
    }
    if (!tbPtr->alreadyReceived)
    {
        // hybrid names (".de" / "de_" prefix): the picture itself is fetched from an FTP server by QSSTV; not done here
        const std::string &fn = tbPtr->fileName;
        const bool hybrid = fn.compare(0, 3, ".de") == 0 || fn.compare(0, 3, "de_") == 0;
        ByteVec out(*baFilePtr);
        RxEvent ev;
        ev.kind = RxEvent::File;
        ev.transportId = tbPtr->transportID;
        ev.name = tbPtr->fileName;
        ev.size = out.size();
        ev.sha256 = sha256ns::sha256_hex(out.data(), out.size());
        ev.fec = fec;
        ev.mode = compactModeToString(tbPtr->modeCode);
        ev.callsign = tbPtr->callsign;
        ev.received = tbPtr->segmentsReceived;
        ev.total = tbPtr->totalSegments;
        ev.snr_db = avgSNR;
        if (hybrid)
            ev.note = "hybrid";
        else if (!outDir.empty())
        {
            if (!writeFile(tbPtr, tbPtr->newFileName, out, ev.savedName, ev.savedPath))
                ev.note = "write failed";
        }
        erasureList.clear();
        saveImage(tbPtr);
        if (sink)
            sink(ev);
    }
    return false;
}

// sourceDecoder::getTransporPtr
transportBlock *sourceDecoder::getTransporPtr(unsigned short tId, bool create)
{
    size_t i;
    bool found = false;
    for (i = 0; i < transportBlockPtrList.size(); i++)
    {
        if (transportBlockPtrList[i]->transportID == tId)
        {
            found = true;
            break;
        }
    }
    if (found)
        lastTransportBlockPtr = transportBlockPtrList[i];
    else if (create)
    {
        callsignValid = false;
        bodyTotalSegments = 0;
        drmBlockList.clear();
        if (transportBlockPtrList.size() >= MAXTRANSPORTLISTS)
        {
            // delete the oldest
            delete transportBlockPtrList.front();
            transportBlockPtrList.erase(transportBlockPtrList.begin());
        }
        for (i = 0; i < transportBlockPtrList.size();)
        {
            if (transportBlockPtrList[i]->fileName == "bsr.bin")
            {
                delete transportBlockPtrList[i];
                transportBlockPtrList.erase(transportBlockPtrList.begin() + i);
            }
            else
                i++;
        }
        transportBlockPtrList.push_back(new transportBlock(tId));
        lastTransportBlockPtr = transportBlockPtrList.back();
        lastTransportBlockPtr->robMode = robustness_mode;
        lastTransportBlockPtr->interLeaver = interleaver_depth_new;
        lastTransportBlockPtr->mscMode = msc_mode_new; // qam
        lastTransportBlockPtr->mpx = multiplex_description.PL_PartB;
        lastTransportBlockPtr->spectrum = spectrum_occupancy_new;
        // remap msc_new to modeCode
        int mCode = 1; // default QAM16
        if (msc_mode_new == 3)
            mCode = 0;
        if (msc_mode_new == 0)
            mCode = 2;
        int protection = 0;
        if (multiplex_description.PL_PartB == 1)
            protection = 1;
        lastTransportBlockPtr->modeCode = (unsigned)(robustness_mode * 10000 + spectrum_occupancy_new * 1000 + protection * 100 + mCode * 10 + interleaver_depth_new);
    }
    else
        return nullptr;
    return lastTransportBlockPtr;
}

// compactModeToString (QSSTV drmstatusframe.cpp)
std::string compactModeToString(unsigned mode)
{
    std::string tmp;
    switch (mode / 10000)
    {
    case 0: tmp += "A"; break;
    case 1: tmp += "B"; break;
    case 2: tmp += "E"; break;
    default: tmp += "-"; break;
    }
    tmp += "/"; // bandwidth
    mode -= (mode / 10000) * 10000;
    switch (mode / 1000)
    {
    case 0: tmp += "2.3"; break;
    case 1: tmp += "2.5"; break;
    default: tmp += "---"; break;
    }
    tmp += "/";
    mode -= (mode / 1000) * 1000;
    switch (mode / 100)
    {
    case 0: tmp += "Hi"; break;
    case 1: tmp += "Lo"; break;
    default: tmp += "--"; break;
    }
    tmp += "/";
    mode -= (mode / 100) * 100;
    switch (mode / 10)
    {
    case 0: tmp += "4"; break;
    case 1: tmp += "16"; break;
    case 2: tmp += "64"; break;
    default: tmp += "--"; break;
    }
    tmp += "/";
    switch (mode & 1)
    {
    case 0: tmp += "Long"; break;
    case 1: tmp += "Short"; break;
    default: tmp += "--"; break;
    }
    return tmp;
}
