#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "OrientalSampler.h"

class KorgKMPEngineProcessor : public juce::AudioProcessor {
public:
    KorgKMPEngineProcessor();
    ~KorgKMPEngineProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    bool loadKMPFile(const juce::File& file);
    void updateVoiceCount();

    OrientalEngineState engineState;
    juce::MidiKeyboardState keyboardState;
    juce::String loadedInstrumentName { "No KMP Loaded" };
    int loadedZonesCount { 0 };

private:
    juce::Synthesiser synth;
    juce::CriticalSection synthLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KorgKMPEngineProcessor)
};
