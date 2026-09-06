#include "StoredZip.h"
#include <array>
#include <limits>
#include <stdexcept>

namespace {
void Write(FILE* f, const void* bytes, size_t n) {
    if (n && fwrite(bytes, 1, n, f) != n) throw std::runtime_error("SD write failed while extracting archive");
}
void U16(FILE* f, uint16_t v) { unsigned char b[] = { (unsigned char)v, (unsigned char)(v >> 8) }; Write(f,b,2); }
void U32(FILE* f, uint32_t v) { U16(f,v); U16(f,v >> 16); }
uint32_t CRC(const std::vector<char>& data) {
    static const auto table = [] { std::array<uint32_t,256> t{}; for (uint32_t i=0;i<256;i++) { uint32_t c=i; for (int j=0;j<8;j++) c=(c>>1)^((c&1)?0xedb88320u:0); t[i]=c; } return t; }();
    uint32_t crc=~0u;
    for (unsigned char c:data) crc=table[(crc^c)&255]^(crc>>8);
    return ~crc;
}
}
StoredZip::StoredZip(const std::string& path) { mPath=path; directoryPath=path+".central.tmp"; }
StoredZip::~StoredZip() { Cleanup(); }

void StoredZip::Cleanup() noexcept {
    if (archive) { fclose(archive); archive = nullptr; }
    if (directory) { fclose(directory); directory = nullptr; }
    if (ownsDirectory) {
        std::remove(directoryPath.c_str());
        ownsDirectory = false;
    }
    if (ownsArchive && !finalized) {
        std::remove(mPath.c_str());
        ownsArchive = false;
    }
}

int32_t StoredZip::CreateArchive() {
    if (ownsArchive || ownsDirectory || finalized)
        throw std::runtime_error("Extraction archive has already been created");
    // Claim each path only after its open succeeds. In particular, failure to
    // open the payload must never create, truncate, or remove a sibling sidecar.
    offset = directorySize = 0;
    count = 0;
    archive = fopen(mPath.c_str(), "wb");
    if (!archive) throw std::runtime_error("Cannot create staged extraction archive");
    ownsArchive = true;
    directory = fopen(directoryPath.c_str(), "w+b");
    if (!directory) {
        Cleanup();
        throw std::runtime_error("Cannot create staged extraction archive");
    }
    ownsDirectory = true;
    return 0;
}
bool StoredZip::AddFile(const std::string& path, std::vector<char> data) {
    if (!archive || !directory || path.size()>65535 || count>=65535 || data.size()>UINT32_MAX ||
        offset+30+path.size()+data.size()>UINT32_MAX || directorySize+46+path.size()>UINT32_MAX)
        throw std::runtime_error("Archive exceeds ZIP32 limits or is closed");
    const uint32_t crc=CRC(data), size=data.size();
    U32(archive,0x04034b50); U16(archive,20); U16(archive,0); U16(archive,0);
    U16(archive,0); U16(archive,0x21); U32(archive,crc); U32(archive,size); U32(archive,size);
    U16(archive,path.size()); U16(archive,0); Write(archive,path.data(),path.size()); Write(archive,data.data(),data.size());
    U32(directory,0x02014b50); U16(directory,20); U16(directory,20); U16(directory,0); U16(directory,0);
    U16(directory,0); U16(directory,0x21); U32(directory,crc); U32(directory,size); U32(directory,size);
    U16(directory,path.size()); U16(directory,0); U16(directory,0); U16(directory,0); U16(directory,0);
    U32(directory,0); U32(directory,offset); Write(directory,path.data(),path.size());
    offset+=30+path.size()+size; directorySize+=46+path.size(); ++count;
    return true;
}
int32_t StoredZip::Close() {
    try {
        if (!archive || !directory || offset+directorySize+22>UINT32_MAX) throw std::runtime_error("Invalid ZIP finalization");
        if (fflush(directory) || fseek(directory,0,SEEK_SET)) throw std::runtime_error("Cannot read archive directory");
        std::array<char,16384> buffer{};
        size_t n;
        while ((n=fread(buffer.data(),1,buffer.size(),directory))) Write(archive,buffer.data(),n);
        if (ferror(directory)) throw std::runtime_error("Cannot read archive directory");
        U32(archive,0x06054b50); U16(archive,0); U16(archive,0); U16(archive,count); U16(archive,count);
        U32(archive,directorySize); U32(archive,offset); U16(archive,0);
        FILE* completed=archive; archive=nullptr;
        if (fclose(completed)) throw std::runtime_error("Cannot finish extraction archive");
        fclose(directory); directory=nullptr;
        finalized = true;
        Cleanup();
        return 0;
    } catch (...) {
        Cleanup();
        throw;
    }
}
