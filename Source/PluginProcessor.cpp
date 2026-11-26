#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    if (!kmp.isValid) return false;

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
