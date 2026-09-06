#include "setup/setup_core.h"

#include <zip.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("soh-setup-core-" + std::to_string(::getpid()));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

void Write(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(out.good());
}

std::vector<uint8_t> Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

void AddZipEntry(zip_t* archive, const char* name, const std::vector<uint8_t>& bytes) {
    void* owned = std::malloc(bytes.size());
    assert(owned != nullptr);
    std::memcpy(owned, bytes.data(), bytes.size());
    zip_source_t* source = zip_source_buffer(archive, owned, bytes.size(), 1);
    assert(source != nullptr);
    assert(zip_file_add(archive, name, source, ZIP_FL_ENC_UTF_8) >= 0);
    assert(zip_set_file_compression(archive, zip_get_num_entries(archive, 0) - 1, ZIP_CM_STORE, 0) == 0);
}

void MakeArchive(const fs::path& path, const std::vector<uint8_t>& portVersion, bool gameArchive) {
    int errorCode = 0;
    zip_t* archive = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errorCode);
    assert(archive != nullptr);
    AddZipEntry(archive, "portVersion", portVersion);
    if (gameArchive) {
        AddZipEntry(archive, "version", { 1, 0x69, 0x3b, 0xa2, 0xae });
        AddZipEntry(archive, "objects/object_link_boy/gLinkAdultSkel", { 4, 8, 15, 16, 23, 42 });
        AddZipEntry(archive, "textures/nintendo_rogo_static/gNintendo64LogoDL", { 1, 2, 3 });
    } else {
        AddZipEntry(archive, "textures/icons/gIcon.png", { 0x53, 0x6f, 0x48 });
    }
    assert(zip_close(archive) == 0);
}

struct CancelAfter {
    size_t limit;
};

bool CancelAtLimit(const char*, size_t done, size_t, void* user) {
    return done < static_cast<CancelAfter*>(user)->limit;
}

void TestNormalizesAllRomByteOrders() {
    TempDir temp;
    const std::vector<uint8_t> canonical = {
        0x80, 0x37, 0x12, 0x40, 0x00, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    };
    const std::array<std::vector<uint8_t>, 3> encodings = {
        canonical,
        std::vector<uint8_t>{ 0x37, 0x80, 0x40, 0x12, 0x01, 0x00, 0x03, 0x02,
                              0x05, 0x04, 0x07, 0x06, 0x09, 0x08, 0x0b, 0x0a },
        std::vector<uint8_t>{ 0x40, 0x12, 0x37, 0x80, 0x03, 0x02, 0x01, 0x00,
                              0x07, 0x06, 0x05, 0x04, 0x0b, 0x0a, 0x09, 0x08 },
    };

    for (size_t i = 0; i < encodings.size(); ++i) {
        const fs::path source = temp.path / ("rom" + std::to_string(i));
        const fs::path normalized = temp.path / ("normalized" + std::to_string(i));
        Write(source, encodings[i]);
        std::string sha1;
        std::string error;
        assert(Soh3dsSetup::NormalizeRom(source.string(), normalized.string(), nullptr, nullptr, sha1, error));
        assert(error.empty());
        assert(Read(normalized) == canonical);
        // Independently calculated with sha1sum over the literal canonical bytes above.
        assert(sha1 == "f8773693ca4dd0297ab51dca23b2df4586a43c99");
    }
}

void TestSha1CoversEveryNormalizedBlock() {
    TempDir temp;
    std::vector<uint8_t> rom(256);
    for (size_t i = 0; i < rom.size(); ++i) rom[i] = static_cast<uint8_t>(i);
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    Write(temp.path / "multi-block.z64", rom);
    std::string sha1;
    std::string error;
    assert(Soh3dsSetup::NormalizeRom((temp.path / "multi-block.z64").string(),
                                    (temp.path / "multi-block-normalized.z64").string(), nullptr, nullptr, sha1,
                                    error));
    // Independently calculated with sha1sum; catches skipped/repeated 64-byte blocks.
    assert(sha1 == "8478f5e17b29ea1c4093aeaf79bb05f4e08b41ff");
}

void TestNormalizationRejectsBadMagicTruncationAndCancellation() {
    TempDir temp;
    std::string sha1;
    std::string error;
    Write(temp.path / "bad.z64", { 0, 1, 2, 3, 4, 5, 6, 7 });
    assert(!Soh3dsSetup::NormalizeRom((temp.path / "bad.z64").string(), (temp.path / "out").string(),
                                     nullptr, nullptr, sha1, error));
    assert(!fs::exists(temp.path / "out"));

    Write(temp.path / "short.n64", { 0x40, 0x12, 0x37 });
    error.clear();
    assert(!Soh3dsSetup::NormalizeRom((temp.path / "short.n64").string(), (temp.path / "out").string(),
                                     nullptr, nullptr, sha1, error));

    Write(temp.path / "keep", { 9, 9, 9 });
    std::vector<uint8_t> rom(256 * 1024, 0x5a);
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    Write(temp.path / "cancel.z64", rom);
    CancelAfter cancel{ 128 * 1024 };
    error.clear();
    assert(!Soh3dsSetup::NormalizeRom((temp.path / "cancel.z64").string(), (temp.path / "keep").string(),
                                     CancelAtLimit, &cancel, sha1, error));
    assert(Read(temp.path / "keep") == std::vector<uint8_t>({ 9, 9, 9 }));
    assert(!fs::exists(temp.path / "keep.part"));
}

void TestMetadataUsesFullSha1AndSafeOutputNames() {
    TempDir temp;
    {
        std::ofstream out(temp.path / "roms.tsv");
        out << "# sha1\tarchive\tdisplay name\n"
            << "f8773693ca4dd0297ab51dca23b2df4586a43c99\toot.o2r\tTest ROM\tntsc_test\n"
            << "0123456789abcdef0123456789abcdef01234567\toot-mq.o2r\tMaster Quest\tmq_test\n";
    }
    std::vector<Soh3dsSetup::RomMetadata> rows;
    std::string error;
    assert(Soh3dsSetup::LoadRomMetadata((temp.path / "roms.tsv").string(), rows, error));
    assert(rows.size() == 2);
    assert(rows[0].displayName == "Test ROM");
    assert(rows[0].metadataVariant == "ntsc_test");
    assert(rows[1].outputArchive == "oot-mq.o2r");
    assert(Soh3dsSetup::FindRomMetadata(rows, "f8773693ca4dd0297ab51dca23b2df4586a43c99") == &rows[0]);
    assert(Soh3dsSetup::FindRomMetadata(rows, "f8773693ca4dd0297ab51dca23b2df4586a43c90") == nullptr);

    {
        std::ofstream out(temp.path / "unsafe.tsv");
        out << "f8773693ca4dd0297ab51dca23b2df4586a43c99\t../oot.o2r\tEscape\n";
    }
    rows.clear();
    error.clear();
    assert(!Soh3dsSetup::LoadRomMetadata((temp.path / "unsafe.tsv").string(), rows, error));
}

void TestArchiveValidationChecksIntegrityRequiredEntriesAndVersion() {
    TempDir temp;
    const std::vector<uint8_t> currentVersion = { 1, 0, 9, 0, 2, 0, 3 };
    MakeArchive(temp.path / "soh.o2r", currentVersion, false);
    MakeArchive(temp.path / "oot.o2r", currentVersion, true);
    std::string error;
    assert(Soh3dsSetup::ValidateArchive((temp.path / "soh.o2r").string(), "9.2.3",
                                       Soh3dsSetup::ArchiveKind::Support, true, error));
    if (!Soh3dsSetup::ValidateArchive((temp.path / "oot.o2r").string(), "9.2.3",
                                     Soh3dsSetup::ArchiveKind::Game, true, error)) {
        std::cerr << "valid game archive rejected: " << error << '\n';
        assert(false);
    }
    CancelAfter cancelValidation{ 1 };
    error.clear();
    assert(!Soh3dsSetup::ValidateArchive((temp.path / "oot.o2r").string(), "9.2.3",
                                        Soh3dsSetup::ArchiveKind::Game, true, error, CancelAtLimit,
                                        &cancelValidation));

    MakeArchive(temp.path / "wrong.o2r", { 1, 0, 9, 0, 1, 0, 0 }, true);
    error.clear();
    assert(!Soh3dsSetup::ValidateArchive((temp.path / "wrong.o2r").string(), "9.2.3",
                                        Soh3dsSetup::ArchiveKind::Game, true, error));

    MakeArchive(temp.path / "no-game-version.o2r", currentVersion, false);
    error.clear();
    assert(!Soh3dsSetup::ValidateArchive((temp.path / "no-game-version.o2r").string(), "9.2.3",
                                        Soh3dsSetup::ArchiveKind::Game, true, error));

    int zipError = 0;
    zip_t* metadataOnly = zip_open((temp.path / "metadata-only.o2r").c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipError);
    assert(metadataOnly != nullptr);
    AddZipEntry(metadataOnly, "portVersion", currentVersion);
    AddZipEntry(metadataOnly, "version", { 1, 0x69, 0x3b, 0xa2, 0xae });
    AddZipEntry(metadataOnly, "decoy", { 1 });
    assert(zip_close(metadataOnly) == 0);
    error.clear();
    assert(!Soh3dsSetup::ValidateArchive((temp.path / "metadata-only.o2r").string(), "9.2.3",
                                        Soh3dsSetup::ArchiveKind::Game, false, error));

    auto corrupt = Read(temp.path / "oot.o2r");
    corrupt.resize(corrupt.size() - 13);
    Write(temp.path / "corrupt.o2r", corrupt);
    error.clear();
    assert(!Soh3dsSetup::ValidateArchive((temp.path / "corrupt.o2r").string(), "9.2.3",
                                        Soh3dsSetup::ArchiveKind::Game, true, error));

    auto badPayload = Read(temp.path / "oot.o2r");
    const std::array<uint8_t, 6> needle = { 4, 8, 15, 16, 23, 42 };
    auto payload = std::search(badPayload.begin(), badPayload.end(), needle.begin(), needle.end());
    assert(payload != badPayload.end());
    *payload ^= 0xff;
    Write(temp.path / "bad-payload.o2r", badPayload);
    error.clear();
    assert(!Soh3dsSetup::ValidateArchive((temp.path / "bad-payload.o2r").string(), "9.2.3",
                                        Soh3dsSetup::ArchiveKind::Game, true, error));
}

void TestStagingPreservesInstalledFileUntilAtomicInstall() {
    TempDir temp;
    Write(temp.path / "source", { 1, 2, 3, 4 });
    Write(temp.path / "installed", { 8, 8, 8 });
    CancelAfter cancel{ 1 };
    std::string error;
    assert(!Soh3dsSetup::CopyFile((temp.path / "source").string(), (temp.path / "installed").string(),
                                 CancelAtLimit, &cancel, error));
    assert(Read(temp.path / "installed") == std::vector<uint8_t>({ 8, 8, 8 }));
    assert(!fs::exists(temp.path / "installed.part"));

    error.clear();
    assert(Soh3dsSetup::CopyFile((temp.path / "source").string(), (temp.path / "staged").string(),
                                nullptr, nullptr, error));
    assert(Read(temp.path / "installed") == std::vector<uint8_t>({ 8, 8, 8 }));
    assert(Soh3dsSetup::AtomicInstall((temp.path / "staged").string(), (temp.path / "installed").string(), error));
    assert(Read(temp.path / "installed") == std::vector<uint8_t>({ 1, 2, 3, 4 }));
    assert(!fs::exists(temp.path / "staged"));
}

void TestDiskHeadroomRejectsImpossibleRequest() {
    TempDir temp;
    uint64_t available = 0;
    std::string error;
    assert(!Soh3dsSetup::HasDiskHeadroom(temp.path.string(), std::numeric_limits<uint64_t>::max(), 0,
                                        available, error));
    assert(available > 0);
}

void TestRetryCleanupRemovesOnlyOwnedStagingArtifacts() {
    TempDir temp;
    const fs::path staging = temp.path / ".setup";
    fs::create_directory(staging);
    Write(staging / "normalized.z64", { 1 });
    Write(staging / "normalized.z64.part", { 2 });
    Write(staging / "oot.o2r", { 3 });
    Write(staging / "oot.o2r.part", { 4 });
    Write(staging / "oot-mq.o2r", { 5 });
    Write(staging / "soh.o2r.part", { 6 });
    Write(staging / "oot.o2r.central.tmp", { 13 });
    Write(staging / "oot-mq.o2r.central.tmp", { 14 });
    fs::create_directories(staging / "torch/ntsc_test");
    Write(staging / "torch/ntsc_test/assets.yml", { 42 });
    Write(staging / "unrelated.keep", { 7 });
    Write(temp.path / "oot.o2r", { 8, 8, 8 });
    Write(temp.path / "source.z64", { 0x80, 0x37, 0x12, 0x40, 9, 10, 11, 12 });

    std::string error;
    assert(Soh3dsSetup::CleanupSetupArtifacts(staging.string(), error));
    assert(!fs::exists(staging / "torch"));
    assert(!fs::exists(staging / "normalized.z64"));
    assert(!fs::exists(staging / "oot.o2r"));
    assert(!fs::exists(staging / "oot-mq.o2r"));
    assert(!fs::exists(staging / "soh.o2r.part"));
    assert(!fs::exists(staging / "oot.o2r.central.tmp"));
    assert(!fs::exists(staging / "oot-mq.o2r.central.tmp"));
    assert(Read(staging / "unrelated.keep") == std::vector<uint8_t>({ 7 }));
    assert(Read(temp.path / "oot.o2r") == std::vector<uint8_t>({ 8, 8, 8 }));
    assert(Read(temp.path / "source.z64") ==
           std::vector<uint8_t>({ 0x80, 0x37, 0x12, 0x40, 9, 10, 11, 12 }));

    // A retry can now stage output under the same known name.
    assert(Soh3dsSetup::CopyFile((temp.path / "source.z64").string(), (staging / "oot.o2r").string(),
                                nullptr, nullptr, error));
    assert(Read(staging / "oot.o2r") == Read(temp.path / "source.z64"));
}

} // namespace

int main(int argc, char** argv) {
    TestNormalizesAllRomByteOrders();
    TestSha1CoversEveryNormalizedBlock();
    TestNormalizationRejectsBadMagicTruncationAndCancellation();
    TestMetadataUsesFullSha1AndSafeOutputNames();
    TestArchiveValidationChecksIntegrityRequiredEntriesAndVersion();
    TestStagingPreservesInstalledFileUntilAtomicInstall();
    TestDiskHeadroomRejectsImpossibleRequest();
    TestRetryCleanupRemovesOnlyOwnedStagingArtifacts();
    if (argc == 3) {
        std::string error;
        if (!Soh3dsSetup::ValidateArchive(argv[1], "9.2.3", Soh3dsSetup::ArchiveKind::Support, true, error)) {
            std::cerr << "real support archive rejected: " << error << '\n';
            return 2;
        }
        if (!Soh3dsSetup::ValidateArchive(argv[2], "9.2.3", Soh3dsSetup::ArchiveKind::Game, false, error)) {
            std::cerr << "real game archive rejected: " << error << '\n';
            return 3;
        }
    }
    std::cout << "setup core tests passed\n";
    return 0;
}
