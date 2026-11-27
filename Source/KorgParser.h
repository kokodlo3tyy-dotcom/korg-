#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

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

class KorgParser {
public:
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
        // دعم بنية سيتات PA (فولدر MULTISMP وبجانبه فولدر PCM)
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
