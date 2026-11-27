#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

KorgKMPEngineProcessor::KorgKMPEngineProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    updateVoiceCount();
}

void KorgKMPEngineProcessor::updateVoiceCount() {
    const juce::ScopedLock sl(synthLock);
    synth.clearVoices();
    int numVoices = engineState.monoMode.load() ? 1 : 16;
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice(new KorgSamplerVoice(engineState));
}

void KorgKMPEngineProcessor::prepareToPlay(double sampleRate, int) {
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

bool KorgKMPEngineProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

bool KorgKMPEngineProcessor::loadSoundEntry(const SoundEntry& entry) {
    KorgMultisample ms;
    if (entry.type == SoundEntry::Type::PA800_PCM) {
        ms = KorgParser::loadPA800Sample(entry.file, entry.sampleIndex, entry.displayName);
    } else {
        ms = KorgParser::loadKMP(entry.file);
    }

    if (!ms.isValid) {
        loadedInstrumentName = "Load Error: " + entry.displayName;
        loadedDetails = "0 Zones";
        return false;
    }

    const juce::ScopedLock sl(synthLock);
    synth.allNotesOff(0, false);
    synth.clearSounds();

    int sr = static_cast<int>(ms.zones[0].sampleRate);
    bool looped = ms.zones[0].hasLoop;
    int zoneCount = static_cast<int>(ms.zones.size());

    for (auto& zone : ms.zones) {
        synth.addSound(new KorgSamplerSound(std::move(zone)));
    }

    loadedInstrumentName = ms.name.isNotEmpty() ? ms.name : entry.displayName;
    if (entry.type == SoundEntry::Type::PA800_PCM) {
        loadedDetails = entry.bankTag + " | " + juce::String(sr) + " Hz | " + (looped ? "LOOP" : "ONE-SHOT");
    } else {
        loadedDetails = juce::String(zoneCount) + " Zones | " + juce::String(sr) + " Hz";
    }
    return true;
}

bool KorgKMPEngineProcessor::loadSingleFile(const juce::File& file) {
    if (!file.existsAsFile()) return false;

    allSounds.clear();
    searchQuery.clear();
    currentFolderName = file.getFileName();

    if (file.hasFileExtension(".pcm")) {
        auto samples = KorgParser::scanPA800PCM(file);
        juce::String bank = file.getFileNameWithoutExtension();
        for (const auto& s : samples) {
            allSounds.push_back({ SoundEntry::Type::PA800_PCM, file, s.indexInFile, s.name, bank });
        }
    } else {
        juce::File parent = file.getParentDirectory();
        currentFolderName = parent.getFileName();
        juce::Array<juce::File> found;
        parent.findChildFiles(found, juce::File::findFiles, false, "*.kmp");

        std::vector<juce::File> sorted;
        for (const auto& f : found) sorted.push_back(f);
        std::sort(sorted.begin(), sorted.end(), [](const juce::File& a, const juce::File& b) {
            return a.getFileName().compareNatural(b.getFileName()) < 0;
        });

        for (const auto& f : sorted) {
            if (!KorgParser::isKorfFile(f)) {
                allSounds.push_back({ SoundEntry::Type::KMP, f, 0, f.getFileNameWithoutExtension(), "KMP" });
            }
        }
    }

    applySearchFilter("");

    if (!filteredIndices.empty()) {
        int targetRow = 0;
        for (int i = 0; i < static_cast<int>(filteredIndices.size()); ++i) {
            if (allSounds[static_cast<size_t>(filteredIndices[static_cast<size_t>(i)])].file == file) {
                targetRow = i;
                break;
            }
        }
        return selectFilteredIndex(targetRow);
    }

    loadedInstrumentName = "No playable sounds found";
    loadedDetails = "Select a PA800 .PCM file or open .SET folder";
    return false;
}

void KorgKMPEngineProcessor::scanFolder(const juce::File& folder) {
    if (!folder.isDirectory()) return;

    allSounds.clear();
    searchQuery.clear();
    currentFilteredRow = -1;
    currentFolderName = folder.getFileName();

    // 1. Scan PA800 .PCM banks
    juce::Array<juce::File> pcmFiles;
    folder.findChildFiles(pcmFiles, juce::File::findFiles, true, "*.pcm");
    std::vector<juce::File> sortedPcm;
    for (const auto& f : pcmFiles) sortedPcm.push_back(f);
    std::sort(sortedPcm.begin(), sortedPcm.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });

    for (const auto& f : sortedPcm) {
        auto samples = KorgParser::scanPA800PCM(f);
        juce::String bank = f.getFileNameWithoutExtension();
        for (const auto& s : samples) {
            allSounds.push_back({ SoundEntry::Type::PA800_PCM, f, s.indexInFile, s.name, bank });
        }
    }

    // 2. Scan standard (non-KORF) .KMP files
    juce::Array<juce::File> kmpFiles;
    folder.findChildFiles(kmpFiles, juce::File::findFiles, true, "*.kmp");
    std::vector<juce::File> sortedKmp;
    for (const auto& f : kmpFiles) sortedKmp.push_back(f);
    std::sort(sortedKmp.begin(), sortedKmp.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });

    for (const auto& f : sortedKmp) {
        if (!KorgParser::isKorfFile(f)) {
            allSounds.push_back({ SoundEntry::Type::KMP, f, 0, f.getFileNameWithoutExtension(), "KMP" });
        }
    }

    applySearchFilter("");

    if (!filteredIndices.empty()) {
        selectFilteredIndex(0);
    } else {
        loadedInstrumentName = "No .PCM or .KMP sounds found";
        loadedDetails = "0 Sounds";
    }
}

void KorgKMPEngineProcessor::applySearchFilter(const juce::String& query) {
    searchQuery = query.trim();
    filteredIndices.clear();

    for (int i = 0; i < static_cast<int>(allSounds.size()); ++i) {
        const auto& s = allSounds[static_cast<size_t>(i)];
        if (searchQuery.isEmpty()
            || s.displayName.containsIgnoreCase(searchQuery)
            || s.bankTag.containsIgnoreCase(searchQuery)) {
            filteredIndices.push_back(i);
        }
    }

    if (filteredIndices.empty()) {
        currentFilteredRow = -1;
    } else if (currentFilteredRow < 0 || currentFilteredRow >= static_cast<int>(filteredIndices.size())) {
        currentFilteredRow = 0;
    }
}

bool KorgKMPEngineProcessor::selectFilteredIndex(int filteredRow) {
    if (filteredRow < 0 || filteredRow >= static_cast<int>(filteredIndices.size()))
        return false;

    currentFilteredRow = filteredRow;
    int realIdx = filteredIndices[static_cast<size_t>(filteredRow)];
    return loadSoundEntry(allSounds[static_cast<size_t>(realIdx)]);
}

void KorgKMPEngineProcessor::selectNextSound() {
    if (filteredIndices.empty()) return;
    int next = (currentFilteredRow + 1) % static_cast<int>(filteredIndices.size());
    selectFilteredIndex(next);
}

void KorgKMPEngineProcessor::selectPreviousSound() {
    if (filteredIndices.empty()) return;
    int total = static_cast<int>(filteredIndices.size());
    int prev = (currentFilteredRow - 1 + total) % total;
    selectFilteredIndex(prev);
}

void KorgKMPEngineProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

    const juce::ScopedLock sl(synthLock);
    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* KorgKMPEngineProcessor::createEditor() {
    return new KorgKMPEngineEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new KorgKMPEngineProcessor();
}
