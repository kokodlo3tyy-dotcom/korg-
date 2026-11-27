#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <cstring>

struct KorgSampleZone {
    juce::String sampleName;
    juce::String ksfFileName;
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    int rootNote = 60;
    int lowNote = 0;
    int highNote = 127;
    int tuneCents = 0;
    float gainLinear = 1.0f;
    int loopStart = 0;
    int loopEnd = 0;
    bool hasLoop = true;
};

struct KorgMultisample {
    juce::String name;
    std::vector<KorgSampleZone> zones;
    bool isValid = false;
};

struct PA800SampleInfo {
    juce::String name;
    int indexInFile = 0;
};

class KorgParser {
public:
    static bool isKorfFile(const juce::File& file) {
        juce::FileInputStream stream(file);
        if (!stream.openedOk()) return false;
        uint8_t header[32] = { 0 };
        if (stream.read(header, 32) < 28) return false;
        return header[0] == 0x01 && std::memcmp(header + 23, "KORF", 4) == 0;
    }

    // Fast TOC scan of a PA800 .PCM file (reads header + embedded 0x05 TOC block)
    static std::vector<PA800SampleInfo> scanPA800PCM(const juce::File& pcmFile) {
        std::vector<PA800SampleInfo> list;
        juce::FileInputStream stream(pcmFile);
        if (!stream.openedOk()) return list;

        uint8_t head[64] = { 0 };
        if (stream.read(head, 64) < 36) return list;

        // Verify KORF signature at offset 0x17 (23) and TOC marker 0x05 at offset 0x1C (28)
        if (head[0] != 0x01 || std::memcmp(head + 23, "KORF", 4) != 0)
            return list;

        // TOC size is stored as 32-bit Big-Endian at offset 0x20 (32)
        uint32_t tocSize = juce::ByteOrder::bigEndianInt(head + 32);
        if (tocSize < 24 || tocSize > 262144)
            return list;

        // TOC entries start at offset 0x24 (36)
        stream.setPosition(36);
        juce::MemoryBlock toc(tocSize);
        if (stream.read(toc.getData(), static_cast<int>(tocSize)) != static_cast<int>(tocSize))
            return list;

        const uint8_t* ptr = static_cast<const uint8_t*>(toc.getData());
        int count = static_cast<int>(tocSize / 24);
        list.reserve(static_cast<size_t>(count));

        for (int i = 0; i < count; ++i) {
            char nameBuf[17] = { 0 };
            std::memcpy(nameBuf, ptr + (i * 24), 16);
            for (int c = 0; c < 16; ++c) {
                if (nameBuf[c] == '\0') break;
                unsigned char ch = static_cast<unsigned char>(nameBuf[c]);
                if (ch < 32 || ch > 126) nameBuf[c] = ' ';
            }
            juce::String sName = juce::String(nameBuf).trim();
            if (sName.isEmpty())
                sName = "Sample " + juce::String(i + 1);

            list.push_back({ sName, i });
        }

        return list;
    }

    // Loads a single sample by index from a PA800 .PCM file (0x12 chunk)
    static KorgMultisample loadPA800Sample(const juce::File& pcmFile, int targetIndex, const juce::String& displayName) {
        KorgMultisample result;
        juce::FileInputStream stream(pcmFile);
        if (!stream.openedOk()) return result;

        uint8_t containerHdr[8] = { 0 };
        if (stream.read(containerHdr, 8) < 8 || containerHdr[0] != 0x01)
            return result;

        int currentIdx = 0;
        while (!stream.isExhausted()) {
            uint8_t chunkHdr[8] = { 0 };
            if (stream.read(chunkHdr, 8) < 8) break;

            uint8_t chunkType = chunkHdr[0];
            uint32_t chunkSize = juce::ByteOrder::bigEndianInt(chunkHdr + 4);
            juce::int64 nextPos = stream.getPosition() + static_cast<juce::int64>(chunkSize);

            if (chunkType == 0x12) {
                if (currentIdx == targetIndex) {
                    if (chunkSize > 66 && chunkSize <= 32 * 1024 * 1024) {
                        juce::MemoryBlock payload(chunkSize);
                        if (stream.read(payload.getData(), static_cast<int>(chunkSize)) == static_cast<int>(chunkSize)) {
                            const uint8_t* p = static_cast<const uint8_t*>(payload.getData());

                            uint32_t rawSR = juce::ByteOrder::bigEndianInt(p + 10);
                            uint32_t rawNumSamples = juce::ByteOrder::bigEndianInt(p + 14);
                            uint32_t rawLoopStart = juce::ByteOrder::bigEndianInt(p + 54);
                            uint32_t rawLoopEnd = juce::ByteOrder::bigEndianInt(p + 58);
                            uint8_t bitHint = p[9];

                            const uint8_t* audioPtr = p + 66;
                            size_t audioBytes = chunkSize - 66;

                            KorgSampleZone zone;
                            zone.sampleName = displayName;
                            zone.sampleRate = (rawSR >= 4000 && rawSR <= 192000) ? static_cast<double>(rawSR) : 44100.0;
                            zone.rootNote = 60;
                            zone.lowNote = 0;
                            zone.highNote = 127;

                            int actualSamples = 0;
                            if (bitHint == 8 && rawNumSamples > 0 && audioBytes >= rawNumSamples) {
                                actualSamples = static_cast<int>(rawNumSamples);
                                zone.buffer.setSize(1, actualSamples);
                                float* dst = zone.buffer.getWritePointer(0);
                                for (int i = 0; i < actualSamples; ++i)
                                    dst[i] = static_cast<int8_t>(audioPtr[i]) / 128.0f;
                            } else {
                                int maxSamples16 = static_cast<int>(audioBytes / 2);
                                actualSamples = (rawNumSamples > 0)
                                    ? juce::jmin(static_cast<int>(rawNumSamples), maxSamples16)
                                    : maxSamples16;

                                if (actualSamples > 0) {
                                    zone.buffer.setSize(1, actualSamples);
                                    float* dst = zone.buffer.getWritePointer(0);
                                    for (int i = 0; i < actualSamples; ++i) {
                                        int16_t val = static_cast<int16_t>(
                                            juce::ByteOrder::bigEndianShort(audioPtr + (i * 2))
                                        );
                                        dst[i] = val / 32768.0f;
                                    }
                                }
                            }

                            if (actualSamples > 1) {
                                zone.loopStart = juce::jlimit(0, actualSamples - 1, static_cast<int>(rawLoopStart));
                                zone.loopEnd = juce::jlimit(zone.loopStart, actualSamples, static_cast<int>(rawLoopEnd));
                                zone.hasLoop = (zone.loopEnd > zone.loopStart + 16);

                                result.name = displayName;
                                result.zones.push_back(std::move(zone));
                                result.isValid = true;
                            }
                        }
                    }
                    break;
                }
                ++currentIdx;
            }

            if (nextPos <= stream.getPosition() || nextPos > stream.getTotalLength())
                break;
            stream.setPosition(nextPos);
        }

        return result;
    }

    static bool loadKSF(const juce::File& ksfFile, KorgSampleZone& zone) {
        if (!ksfFile.existsAsFile()) return false;

        juce::MemoryBlock data;
        if (!ksfFile.loadFileAsData(data)) return false;

        const uint8_t* ptr = static_cast<const uint8_t*>(data.getData());
        size_t size = data.getSize();
        size_t offset = 0;

        int numSamples = 0;
        int bitDepth = 16;
        int numChannels = 1;
        uint32_t rawLoopStart = 0;
        uint32_t rawLoopEnd = 0;
        bool sampleLoaded = false;

        while (offset + 8 <= size) {
            juce::String chunkID(reinterpret_cast<const char*>(ptr + offset), 4);
            uint32_t chunkSize = juce::ByteOrder::bigEndianInt(ptr + offset + 4);
            offset += 8;

            if (offset + chunkSize > size) break;

            if (chunkID == "SMP1" && chunkSize >= 28) {
                zone.sampleName = juce::String(reinterpret_cast<const char*>(ptr + offset), 16).trim();
                rawLoopStart = juce::ByteOrder::bigEndianInt(ptr + offset + 20);
                rawLoopEnd = juce::ByteOrder::bigEndianInt(ptr + offset + 24);
            }
            else if (chunkID == "SMD1" && chunkSize >= 12) {
                zone.sampleRate = static_cast<double>(juce::ByteOrder::bigEndianInt(ptr + offset));
                if (zone.sampleRate <= 0.0) zone.sampleRate = 44100.0;

                uint8_t attributes = ptr[offset + 4];
                zone.hasLoop = (attributes & 0x01) == 0;
                numChannels = juce::jmax(1, static_cast<int>(ptr[offset + 6]));
                bitDepth = static_cast<int>(ptr[offset + 7]);
                numSamples = static_cast<int>(juce::ByteOrder::bigEndianInt(ptr + offset + 8));

                const uint8_t* pcm = ptr + offset + 12;
                size_t availableBytes = chunkSize - 12;

                if (bitDepth == 16 && numSamples > 0) {
                    int maxPossibleSamples = static_cast<int>(availableBytes / (2 * numChannels));
                    numSamples = juce::jmin(numSamples, maxPossibleSamples);

                    zone.buffer.setSize(1, numSamples);
                    float* dst = zone.buffer.getWritePointer(0);

                    for (int i = 0; i < numSamples; ++i) {
                        int16_t val = static_cast<int16_t>(
                            juce::ByteOrder::bigEndianShort(pcm + (i * 2 * numChannels))
                        );
                        dst[i] = val / 32768.0f;
                    }
                    sampleLoaded = true;
                }
            }
            offset += chunkSize;
        }

        if (sampleLoaded && numSamples > 0) {
            zone.loopStart = juce::jlimit(0, numSamples - 1, static_cast<int>(rawLoopStart));
            int endCandidate = (rawLoopEnd > 0) ? static_cast<int>(rawLoopEnd) : numSamples;
            zone.loopEnd = juce::jlimit(zone.loopStart + 1, numSamples, endCandidate);
            return true;
        }
        return false;
    }

    static KorgMultisample loadKMP(const juce::File& kmpFile) {
        KorgMultisample result;
        if (!kmpFile.existsAsFile()) return result;

        juce::MemoryBlock data;
        if (!kmpFile.loadFileAsData(data)) return result;

        const uint8_t* ptr = static_cast<const uint8_t*>(data.getData());
        size_t size = data.getSize();
        size_t offset = 0;

        int numZones = 0;
        juce::File parentDir = kmpFile.getParentDirectory();
        juce::File subDir = parentDir.getChildFile(kmpFile.getFileNameWithoutExtension());
        juce::File pcmDir = parentDir.getParentDirectory().getChildFile("PCM");
        juce::File pcmSubDir = pcmDir.getChildFile(kmpFile.getFileNameWithoutExtension());

        while (offset + 8 <= size) {
            juce::String chunkID(reinterpret_cast<const char*>(ptr + offset), 4);
            uint32_t chunkSize = juce::ByteOrder::bigEndianInt(ptr + offset + 4);
            offset += 8;

            if (offset + chunkSize > size) break;

            if (chunkID == "MSP1" && chunkSize >= 17) {
                result.name = juce::String(reinterpret_cast<const char*>(ptr + offset), 16).trim();
                numZones = static_cast<int>(ptr[offset + 16]);
            }
            else if (chunkID == "RLP1" && numZones > 0) {
                int entrySize = static_cast<int>(chunkSize) / numZones;
                if (entrySize >= 18) {
                    int prevTopKey = -1;
                    for (int i = 0; i < numZones; ++i) {
                        const uint8_t* entry = ptr + offset + (i * entrySize);

                        KorgSampleZone zone;
                        zone.rootNote = entry[0] & 0x7F;
                        zone.highNote = entry[1] & 0x7F;
                        zone.lowNote = prevTopKey + 1;
                        prevTopKey = zone.highNote;

                        zone.tuneCents = static_cast<int8_t>(entry[2]);
                        int levelDb = static_cast<int8_t>(entry[3]);
                        zone.gainLinear = juce::Decibels::decibelsToGain(static_cast<float>(levelDb) * 0.5f);

                        juce::String rawName = juce::String(reinterpret_cast<const char*>(entry + 6), 12).trim();
                        if (!rawName.endsWithIgnoreCase(".KSF"))
                            rawName += ".KSF";
                        zone.ksfFileName = rawName;

                        juce::File ksfFile = parentDir.getChildFile(rawName);
                        if (!ksfFile.existsAsFile()) ksfFile = subDir.getChildFile(rawName);
                        if (!ksfFile.existsAsFile()) ksfFile = pcmDir.getChildFile(rawName);
                        if (!ksfFile.existsAsFile()) ksfFile = pcmSubDir.getChildFile(rawName);

                        if (loadKSF(ksfFile, zone)) {
                            result.zones.push_back(std::move(zone));
                        }
                    }
                }
            }
            offset += chunkSize;
        }

        result.isValid = !result.zones.empty();
        return result;
    }
};
