#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "OrientalSampler.h"
#include <vector>

struct SoundEntry {
    enum class Type { KMP, PA800_PCM };
    Type type = Type::KMP;
    juce::File file;
    int sampleIndex = 0;
    juce::String displayName;
    juce::String bankTag;
};

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

    bool loadSingleFile(const juce::File& file);
    void scanFolder(const juce::File& folder);
    void applySearchFilter(const juce::String& query);
    bool selectFilteredIndex(int filteredRow);
    void selectNextSound();
    void selectPreviousSound();
    void updateVoiceCount();

    OrientalEngineState engineState;
    juce::MidiKeyboardState keyboardState;
    juce::String loadedInstrumentName { "No Sound Loaded" };
    juce::String loadedDetails { "Open a .SET / PCM folder or .KMP file" };

    std::vector<SoundEntry> allSounds;
    std::vector<int> filteredIndices;
    int currentFilteredRow { -1 };
    juce::String searchQuery;
    juce::String currentFolderName { "No Folder Selected" };

private:
    bool loadSoundEntry(const SoundEntry& entry);

    juce::Synthesiser synth;
    juce::CriticalSection synthLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KorgKMPEngineProcessor)
};
