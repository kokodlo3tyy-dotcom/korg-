#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <array>

class KorgKMPEngineEditor : public juce::AudioProcessorEditor {
public:
    explicit KorgKMPEngineEditor(KorgKMPEngineProcessor&);
    ~KorgKMPEngineEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void applyMaqamPreset(const std::array<float, 12>& cents);
    void refreshQuarterToneButtons();

    KorgKMPEngineProcessor& processor;

    juce::TextButton loadButton { "LOAD .KMP" };
    juce::Label titleLabel;
    juce::Label statusLabel;

    // Oriental Quarter-Tone (-50 cents) Buttons
    std::array<juce::TextButton, 12> noteTuneButtons;
    juce::TextButton presetClear { "12-TET" };
    juce::TextButton presetBayatiD { "Bayati D" };
    juce::TextButton presetRastC { "Rast C" };
    juce::TextButton presetSabaD { "Saba D" };

    // Engine Controls
    juce::ToggleButton monoToggle { "MONO / LEGATO" };
    juce::Slider glideSlider, attackSlider, releaseSlider, gainSlider;
    juce::Label glideLabel, attackLabel, releaseLabel, gainLabel;
    juce::ComboBox pbRangeBox;
    juce::Label pbLabel;

    juce::MidiKeyboardComponent midiKeyboard;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KorgKMPEngineEditor)
};
