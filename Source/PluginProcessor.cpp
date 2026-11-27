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

bool KorgKMPEngineProcessor::loadKMPFile(const juce::File& file) {
    auto kmp = KorgParser::loadKMP(file);
    if (!kmp.isValid) {
        loadedInstrumentName = "Error loading KSF: " + file.getFileNameWithoutExtension();
        loadedZonesCount = 0;
        return false;
    }

    const juce::ScopedLock sl(synthLock);
    synth.allNotesOff(0, false);
    synth.clearSounds();

    for (auto& zone : kmp.zones) {
        synth.addSound(new KorgSamplerSound(std::move(zone)));
    }

    loadedInstrumentName = kmp.name.isNotEmpty() ? kmp.name : file.getFileNameWithoutExtension();
    loadedZonesCount = static_cast<int>(kmp.zones.size());
    return true;
}

bool KorgKMPEngineProcessor::loadSingleKMPWithSiblings(const juce::File& file) {
    juce::File parent = file.getParentDirectory();
    kmpFiles.clear();
    currentFolderName = parent.getFileName();

    juce::Array<juce::File> found;
    parent.findChildFiles(found, juce::File::findFiles, false, "*.kmp");

    for (const auto& f : found)
        kmpFiles.push_back(f);

    std::sort(kmpFiles.begin(), kmpFiles.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });

    currentSoundIndex = -1;
    for (int i = 0; i < static_cast<int>(kmpFiles.size()); ++i) {
        if (kmpFiles[static_cast<size_t>(i)] == file) {
            currentSoundIndex = i;
            break;
        }
    }

    return loadKMPFile(file);
}

void KorgKMPEngineProcessor::scanFolderForKMP(const juce::File& folder) {
    if (!folder.isDirectory()) return;

    kmpFiles.clear();
    currentSoundIndex = -1;
    currentFolderName = folder.getFileName();

    juce::Array<juce::File> found;
    folder.findChildFiles(found, juce::File::findFiles, true, "*.kmp");

    for (const auto& f : found)
        kmpFiles.push_back(f);

    std::sort(kmpFiles.begin(), kmpFiles.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });

    if (!kmpFiles.empty()) {
        selectSoundByIndex(0);
    } else {
        loadedInstrumentName = "No .KMP files in folder";
        loadedZonesCount = 0;
    }
}

bool KorgKMPEngineProcessor::selectSoundByIndex(int index) {
    if (index < 0 || index >= static_cast<int>(kmpFiles.size()))
        return false;

    currentSoundIndex = index;
    return loadKMPFile(kmpFiles[static_cast<size_t>(index)]);
}

void KorgKMPEngineProcessor::selectNextSound() {
    if (kmpFiles.empty()) return;
    int next = (currentSoundIndex + 1) % static_cast<int>(kmpFiles.size());
    selectSoundByIndex(next);
}

void KorgKMPEngineProcessor::selectPreviousSound() {
    if (kmpFiles.empty()) return;
    int total = static_cast<int>(kmpFiles.size());
    int prev = (currentSoundIndex - 1 + total) % total;
    selectSoundByIndex(prev);
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
