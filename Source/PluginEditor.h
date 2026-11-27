#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <array>

class KorgKMPEngineEditor : public juce::AudioProcessorEditor,
                             private juce::ListBoxModel {
public:
    explicit KorgKMPEngineEditor(KorgKMPEngineProcessor&);
    ~KorgKMPEngineEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ListBoxModel overrides
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

private:
    void applyMaqamPreset(const std::array<float, 12>& cents);
    void refreshQuarterToneButtons();
    void syncBrowserAndStatus();

    KorgKMPEngineProcessor& processor;
    bool isUpdatingSelection = false;

    // Left Sidebar: Sound Browser
    juce::Label browserTitleLabel;
    juce::Label folderNameLabel;
    juce::TextButton openFolderButton { "OPEN SET / FOLDER" };
    juce::TextButton prevSoundButton { "<" };
    juce::TextButton nextSoundButton { ">" };
    juce::TextEditor searchBox;
    juce::ListBox soundListBox;

    // Main Top Header
    juce::TextButton loadButton { "LOAD FILE" };
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
