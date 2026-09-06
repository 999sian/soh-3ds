#include <ship/Context.h>
#include <ship/resource/archive/Archive.h>
#include <ship/resource/ResourceManager.h>
#include <spdlog/spdlog.h>

#include "soh/resource/importer/AudioSampleFactory.h"
#include "soh/resource/importer/AudioSoundFontFactory.h"
#include "soh/resource/type/AudioSample.h"

extern "C" {
#include "z64.h"
#include "z64audio.h"
}

#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include "vorbis/vorbisfile.h"
#include <tinyxml2.h>
#include <algorithm>
#include <vector>
#include <stdexcept>

struct OggFileData {
    const void* data;
    size_t pos;
    size_t size;
};

static size_t VorbisReadCallback(void* out, size_t size, size_t elems, void* src) {
    auto* data = static_cast<OggFileData*>(src);
    if (size == 0 || data->pos > data->size) return 0;
    const size_t count = std::min(elems, (data->size - data->pos) / size);
    memcpy(out, static_cast<const uint8_t*>(data->data) + data->pos, count * size);
    data->pos += count * size;
    return count;
}

static int VorbisSeekCallback(void* src, ogg_int64_t offset, int whence) {
    auto* data = static_cast<OggFileData*>(src);
    size_t base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = data->pos; break;
        case SEEK_END: base = data->size; break;
        default: return -1;
    }
    if (offset < 0) {
        const uint64_t back = uint64_t(-(offset + 1)) + 1;
        if (back > base) return -1;
        data->pos = base - back;
    } else {
        if (uint64_t(offset) > data->size - base) return -1;
        data->pos = base + offset;
    }
    return 0;
}

static int VorbisCloseCallback(void*) { return 0; }
static long VorbisTellCallback(void* src) { return static_cast<long>(static_cast<OggFileData*>(src)->pos); }
static const ov_callbacks vorbisCallbacks = {
    VorbisReadCallback, VorbisSeekCallback, VorbisCloseCallback, VorbisTellCallback,
};

enum class OggType { None, Vorbis, Opus };
static OggType GetOggType(OggFileData* data) {
    ogg_sync_state sync{};
    if (ogg_sync_init(&sync) != 0) return OggType::None;
    auto syncCleanup = std::unique_ptr<ogg_sync_state, decltype(&ogg_sync_clear)>(&sync, ogg_sync_clear);
    const size_t bytes = std::min(data->size, size_t(4096));
    char* buffer = ogg_sync_buffer(&sync, bytes);
    if (!buffer) return OggType::None;
    memcpy(buffer, data->data, bytes);
    if (ogg_sync_wrote(&sync, bytes) != 0) return OggType::None;
    ogg_page page{};
    if (ogg_sync_pageout(&sync, &page) != 1) return OggType::None;
    ogg_stream_state stream{};
    if (ogg_stream_init(&stream, ogg_page_serialno(&page)) != 0) return OggType::None;
    auto streamCleanup = std::unique_ptr<ogg_stream_state, decltype(&ogg_stream_clear)>(&stream, ogg_stream_clear);
    ogg_packet packet{};
    if (ogg_stream_pagein(&stream, &page) != 0 || ogg_stream_packetout(&stream, &packet) != 1)
        return OggType::None;
    if (packet.bytes >= 7 && memcmp(packet.packet, "\x01vorbis", 7) == 0) return OggType::Vorbis;
    if (packet.bytes >= 8 && memcmp(packet.packet, "OpusHead", 8) == 0) return OggType::Opus;
    return OggType::None;
}

static size_t CheckedPcmBytes(uint64_t frames, uint64_t channels) {
    if (frames == 0 || channels == 0 || channels > 2 ||
        frames > SOH::AudioSample::MaxSampleBytes / sizeof(int16_t) / channels) {
        throw std::runtime_error("Invalid or oversized decoded audio sample");
    }
    return static_cast<size_t>(frames * channels * sizeof(int16_t));
}

static size_t CheckedBookEntries(int32_t order, int32_t predictors) {
    if (order < 0 || order > 2 || predictors < 0 || predictors > 8 || ((order == 0) != (predictors == 0)))
        throw std::runtime_error("Invalid ADPCM book dimensions");
    return size_t(order) * size_t(predictors) * 8;
}

// Decoding completes before the factory returns, so no consumer can observe partial PCM.
// The budget belongs to AudioSample and covers both in-flight and retained custom buffers.
static bool DecodeCustomAudio(SOH::AudioSample& sample, const std::vector<char>& encoded,
                              const char* format) noexcept {
    try {
        uint64_t frames = 0, channels = 0, rate = 0;
        size_t bytes = 0;
        if (encoded.empty() || encoded.size() > UINT32_MAX) throw std::runtime_error("Invalid audio file size");
        if (strcmp(format, "wav") == 0) {
            drwav wav{};
            if (!drwav_init_memory(&wav, encoded.data(), encoded.size(), nullptr))
                throw std::runtime_error("Invalid WAV");
            auto cleanup = std::unique_ptr<drwav, decltype(&drwav_uninit)>(&wav, drwav_uninit);
            drwav_uint64 wavFrames = 0;
            if (drwav_get_length_in_pcm_frames(&wav, &wavFrames) != DRWAV_SUCCESS)
                throw std::runtime_error("Invalid WAV length");
            frames = wavFrames; channels = wav.channels; rate = wav.sampleRate;
            bytes = CheckedPcmBytes(frames, channels);
            sample.AllocateSampleData(bytes, true);
            if (drwav_read_pcm_frames_s16(&wav, frames, reinterpret_cast<int16_t*>(sample.sample.sampleAddr)) != frames)
                throw std::runtime_error("Truncated WAV");
        } else if (strcmp(format, "mp3") == 0) {
            drmp3 mp3{};
            if (!drmp3_init_memory(&mp3, encoded.data(), encoded.size(), nullptr))
                throw std::runtime_error("Invalid MP3");
            auto cleanup = std::unique_ptr<drmp3, decltype(&drmp3_uninit)>(&mp3, drmp3_uninit);
            frames = drmp3_get_pcm_frame_count(&mp3); channels = mp3.channels; rate = mp3.sampleRate;
            bytes = CheckedPcmBytes(frames, channels);
            sample.AllocateSampleData(bytes, true);
            if (!drmp3_seek_to_pcm_frame(&mp3, 0) ||
                drmp3_read_pcm_frames_s16(&mp3, frames, reinterpret_cast<int16_t*>(sample.sample.sampleAddr)) != frames)
                throw std::runtime_error("Truncated MP3");
        } else if (strcmp(format, "flac") == 0) {
            auto flac = std::unique_ptr<drflac, decltype(&drflac_close)>(
                drflac_open_memory(encoded.data(), encoded.size(), nullptr), drflac_close);
            if (!flac) throw std::runtime_error("Invalid FLAC");
            frames = flac->totalPCMFrameCount; channels = flac->channels; rate = flac->sampleRate;
            bytes = CheckedPcmBytes(frames, channels);
            sample.AllocateSampleData(bytes, true);
            if (drflac_read_pcm_frames_s16(flac.get(), frames, reinterpret_cast<int16_t*>(sample.sample.sampleAddr)) != frames)
                throw std::runtime_error("Truncated FLAC");
        } else if (strcmp(format, "ogg") == 0) {
            OggFileData fileData{encoded.data(), 0, encoded.size()};
            const OggType type = GetOggType(&fileData);
            if (type == OggType::Opus) {
                // OPUS remains compressed until the audio driver decodes it.
                if (sample.sample.size == 0) throw std::runtime_error("Invalid OPUS sample size");
                sample.AllocateSampleData(encoded.size(), true);
                memcpy(sample.sample.sampleAddr, encoded.data(), encoded.size());
                sample.sample.codec = CODEC_OPUS;
                return true;
            }
            if (type != OggType::Vorbis) throw std::runtime_error("Invalid Ogg stream");
            OggVorbis_File vf{};
            if (ov_open_callbacks(&fileData, &vf, nullptr, 0, vorbisCallbacks) != 0)
                throw std::runtime_error("Invalid Vorbis");
            auto cleanup = std::unique_ptr<OggVorbis_File, decltype(&ov_clear)>(&vf, ov_clear);
            const auto* info = ov_info(&vf, -1);
            const auto total = ov_pcm_total(&vf, -1);
            if (!info || total <= 0 || info->rate <= 0) throw std::runtime_error("Invalid Vorbis length");
            frames = total; channels = info->channels; rate = info->rate;
            // ov_streams returns this public field; the 3DS compatibility shim
            // does not provide that extra function. Validate all seekable links.
            if (vf.links <= 0) throw std::runtime_error("Invalid Vorbis link count");
            for (int link = 0; link < vf.links; ++link) {
                const auto* linked = ov_info(&vf, link);
                if (!linked || linked->channels != channels || linked->rate != rate)
                    throw std::runtime_error("Vorbis format changes within sample");
            }
            bytes = CheckedPcmBytes(frames, channels);
            sample.AllocateSampleData(bytes, true);
            size_t pos = 0;
            int bitStream = 0;
            char buffer[4096];
            for (;;) {
                const long read = ov_read(&vf, buffer, sizeof(buffer), 0, 2, 1, &bitStream);
                if (read < 0 || size_t(read) > bytes - pos) throw std::runtime_error("Invalid Vorbis PCM");
                if (read == 0) break;
                memcpy(sample.sample.sampleAddr + pos, buffer, read);
                pos += read;
            }
            if (pos != bytes) throw std::runtime_error("Truncated Vorbis");
        } else {
            throw std::runtime_error("Unsupported custom audio format");
        }
        if (rate == 0) throw std::runtime_error("Invalid sample rate");
        sample.sample.codec = CODEC_S16;
        sample.sample.size = static_cast<uint32_t>(bytes);
        // WAV has historically supplied tuning; other formats retain soundfont tuning.
        if (strcmp(format, "wav") == 0) sample.tuning = (rate * channels) / 32000.0f;
        const uint32_t samples = bytes / sizeof(int16_t);
        if (sample.loop.end == 0 || sample.loop.end > samples) sample.loop.end = samples;
        if (sample.loop.start >= sample.loop.end) { sample.loop.start = 0; sample.loop.count = 0; }
        sample.sample.loop = &sample.loop;
        return true;
    } catch (...) {
        sample.MarkUnavailable();
        return false;
    }
}

namespace SOH {
std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryAudioSampleV2::ReadResource(std::shared_ptr<Ship::File> file,
                                                 std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto audioSample = std::make_shared<AudioSample>(initData);
    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);

    audioSample->sample.codec = reader->ReadUByte();
    audioSample->sample.medium = reader->ReadUByte();
    audioSample->sample.unk_bit26 = reader->ReadUByte();
    audioSample->sample.isRelocated = reader->ReadUByte();
    audioSample->sample.size = reader->ReadUInt32();

    audioSample->AllocateSampleData(audioSample->sample.size);
    for (uint32_t i = 0; i < audioSample->sample.size; i++) {
        audioSample->sample.sampleAddr[i] = reader->ReadUByte();
    }

    audioSample->loop.start = reader->ReadUInt32();
    audioSample->loop.end = reader->ReadUInt32();
    audioSample->loop.count = reader->ReadUInt32();

    // This always seems to be 16. Can it be removed in V3?
    uint32_t loopStateCount = reader->ReadUInt32();
    if (loopStateCount > 16) throw std::runtime_error("Invalid ADPCM loop state count");
    for (uint32_t i = 0; i < loopStateCount; i++) {
        audioSample->loop.state[i] = reader->ReadInt16();
    }
    audioSample->sample.loop = &audioSample->loop;

    audioSample->book.order = reader->ReadInt32();
    audioSample->book.npredictors = reader->ReadInt32();
    uint32_t bookDataCount = reader->ReadUInt32();

    const size_t expectedEntries = CheckedBookEntries(audioSample->book.order, audioSample->book.npredictors);
    if (bookDataCount != expectedEntries) throw std::runtime_error("Invalid ADPCM book count");
    audioSample->AllocateBookData(bookDataCount);

    for (uint32_t i = 0; i < bookDataCount; i++) {
        audioSample->book.book[i] = reader->ReadInt16();
    }
    audioSample->sample.book = &audioSample->book;

    return audioSample;
}

std::shared_ptr<Ship::IResource>
ResourceFactoryXMLAudioSampleV0::ReadResource(std::shared_ptr<Ship::File> file,
                                              std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto audioSample = std::make_shared<AudioSample>(initData);
    auto child = std::get<std::shared_ptr<tinyxml2::XMLDocument>>(file->Reader)->FirstChildElement();
    if (!child) throw std::runtime_error("Missing audio sample element");
    const char* customFormatStr = child->Attribute("CustomFormat");
    audioSample->sample.isRelocated = 0;
    audioSample->sample.codec = CodecStrToInt(child->Attribute("Codec"), initData->Path.c_str());
    const char* medium = child->Attribute("Medium");
    if (!medium) throw std::runtime_error("Missing audio medium");
    audioSample->sample.medium =
        ResourceFactoryXMLSoundFontV0::MediumStrToInt(medium, initData->Path.c_str());
    audioSample->sample.unk_bit26 = child->IntAttribute("bit26");

    tinyxml2::XMLElement* loopRoot = child->FirstChildElement("ADPCMLoop");
    if (loopRoot != nullptr) {
        size_t i = 0;
        audioSample->loop.start = loopRoot->UnsignedAttribute("Start");
        audioSample->loop.end = loopRoot->UnsignedAttribute("End");
        audioSample->loop.count = loopRoot->UnsignedAttribute("Count");
        tinyxml2::XMLElement* predictor = loopRoot->FirstChildElement("Predictor");
        while (predictor != nullptr) {
            if (i >= 16) throw std::runtime_error("Invalid ADPCM loop state count");
            audioSample->loop.state[i++] = predictor->IntAttribute("State");
            predictor = predictor->NextSiblingElement();
        }
    }

    tinyxml2::XMLElement* bookRoot = child->FirstChildElement("ADPCMBook");
    if (bookRoot != nullptr) {
        size_t i = 0;
        audioSample->book.npredictors = bookRoot->IntAttribute("Npredictors");
        audioSample->book.order = bookRoot->IntAttribute("Order");
        tinyxml2::XMLElement* book = bookRoot->FirstChildElement("Book");
        size_t numBooks = CheckedBookEntries(audioSample->book.order, audioSample->book.npredictors);
        audioSample->AllocateBookData(numBooks);
        while (book != nullptr) {
            if (i >= numBooks) throw std::runtime_error("Invalid ADPCM book count");
            audioSample->book.book[i++] = book->IntAttribute("Page");
            book = book->NextSiblingElement();
        }
        if (i != numBooks) throw std::runtime_error("Incomplete ADPCM book");
        audioSample->sample.book = &audioSample->book;
    }

    audioSample->sample.loop = &audioSample->loop;
    const int64_t requestedSize = child->Int64Attribute("Size");
    if (requestedSize < 0 || requestedSize > UINT32_MAX) throw std::runtime_error("Invalid audio sample size");
    size_t size = static_cast<size_t>(requestedSize);
    audioSample->sample.size = static_cast<u32>(size);

    const char* path = child->Attribute("Path");

    if (!path) throw std::runtime_error("Missing audio sample path");
    auto sampleFile = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->LoadFile(path);
    if (!sampleFile || !sampleFile->Buffer || sampleFile->Buffer->size() > UINT32_MAX) {
        if (customFormatStr) { audioSample->MarkUnavailable(); return audioSample; }
        throw std::runtime_error("Missing or oversized audio file");
    }
    audioSample->sample.fileSize = static_cast<u32>(sampleFile->Buffer->size());
    if (customFormatStr != nullptr) {
        if (!DecodeCustomAudio(*audioSample, *sampleFile->Buffer, customFormatStr)) {
            SPDLOG_ERROR("Custom audio unavailable: {} ({})", initData->Path, customFormatStr);
        }
        return audioSample;
    }
    // Not a normal streamed sample. Fallback to the original ADPCM sample to be decoded by the audio engine.
    if (size > sampleFile->Buffer->size()) throw std::runtime_error("Truncated audio file");
    audioSample->AllocateSampleData(size);
    // Can't use memcpy due to endianness issues.
    for (uint32_t i = 0; i < size; i++) {
        audioSample->sample.sampleAddr[i] = sampleFile->Buffer.get()->data()[i];
    }

    return audioSample;
}

uint8_t ResourceFactoryXMLAudioSampleV0::CodecStrToInt(const char* str, const char* file) {
    if (!str) throw std::runtime_error("Missing audio codec");
    if (strcmp("ADPCM", str) == 0) {
        return CODEC_ADPCM;
    } else if (strcmp("S8", str) == 0) {
        return CODEC_S8;
    } else if (strcmp("S16MEM", str) == 0) {
        return CODEC_S16_INMEMORY;
    } else if (strcmp("ADPCMSMALL", str) == 0) {
        return CODEC_SMALL_ADPCM;
    } else if (strcmp("REVERB", str) == 0) {
        return CODEC_REVERB;
    } else if (strcmp("S16", str) == 0) {
        return CODEC_S16;
    } else {
        char buff[2048];
        snprintf(buff, 2048, "Invalid codec in %s. Got %s, expected ADPCM, S8, S16MEM, ADPCMSMALL, REVERB, S16.", file,
                 str);
        throw std::runtime_error(buff);
    }
}
} // namespace SOH
