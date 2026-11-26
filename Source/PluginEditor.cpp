#include "PluginEditor.h"

namespace {
    const juce::Colour bgDark     { 0xff121316 };
    const juce::Colour panelDark  { 0xff1b1d22 };
    const juce::Colour goldAccent { 0xffd4af37 };
    const juce::Colour textLight  { 0xfff0f0f0 };
    const juce::Colour textMuted  { 0xff8c929d };
}

KorgKMPEngineEditor::KorgKMPEngineEditor(KorgKMPEngineProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      midiKeyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {

    setSize(760, 440);

    // Header
    titleLabel.setText("MIDO MAZZIKA // KORG KMP ENGINE", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, goldAccent);
    addAndMakeVisible(titleLabel);

    statusLabel.setText(processor.loadedInstrumentName + " (" + juce::String(processor.loadedZonesCount) + " Zones)",
                        juce::dontSendNotification);
    statusLabel.setFont(juce::FontOptions(14.0f));
    statusLabel.setColour(juce::Label::textColourId, textLight);
    addAndMakeVisible(statusLabel);

    loadButton.setColour(juce::TextButton::buttonColourId, goldAccent);
    loadButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    loadButton.onClick = [this] {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select Korg Multisample (.KMP)",
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
            "*.kmp;*.KMP"
        );
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file.existsAsFile()) {
                if (processor.loadKMPFile(file)) {
                    statusLabel.setText(processor.loadedInstrumentName + " (" + juce::String(processor.loadedZonesCount) + " Zones)",
                                        juce::dontSendNotification);
                } else {
                    statusLabel.setText("Error: Could not load KSF samples for " + file.getFileName(),
                                        juce::dontSendNotification);
                }
            }
        });
    };
    addAndMakeVisible(loadButton);

    // 12-Note Quarter-Tone Strip
    const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i) {
        noteTuneButtons[i].setButtonText(noteNames[i]);
        noteTuneButtons[i].onClick = [this, i] {
            float cur = processor.engineState.microtuneCents[i].load();
            processor.engineState.microtuneCents[i].store(cur < -10.0f ? 0.0f : -50.0f);
            refreshQuarterToneButtons();
        };
        addAndMakeVisible(noteTuneButtons[i]);
    }

    auto setupPresetBtn = [this](juce::TextButton& btn, std::array<float, 12> map) {
        btn.setColour(juce::TextButton::buttonColourId, panelDark);
        btn.setColour(juce::TextButton::textColourOffId, goldAccent);
        btn.onClick = [this, map] { applyMaqamPreset(map); };
        addAndMakeVisible(btn);
    };

    setupPresetBtn(presetClear,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
    setupPresetBtn(presetBayatiD, { 0, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -50 });
    setupPresetBtn(presetRastC,   { 0, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -50 });
    setupPresetBtn(presetSabaD,   { 0, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, 0 });

    refreshQuarterToneButtons();

    // Mono / Legato Toggle
    monoToggle.setToggleState(processor.engineState.monoMode.load(), juce::dontSendNotification);
    monoToggle.setColour(juce::ToggleButton::textColourId, textLight);
    monoToggle.setColour(juce::ToggleButton::tickColourId, goldAccent);
    monoToggle.onClick = [this] {
        processor.engineState.monoMode.store(monoToggle.getToggleState());
        processor.updateVoiceCount();
    };
    addAndMakeVisible(monoToggle);

    // Rotary Knobs Helper
    auto setupKnob = [this](juce::Slider& s, juce::Label& l, const juce::String& name,
                            double minV, double maxV, double initV, std::function<void(double)> onChange) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        s.setRange(minV, maxV, 0.001);
        s.setValue(initV, juce::dontSendNotification);
        s.setColour(juce::Slider::rotarySliderFillColourId, goldAccent);
        s.setColour(juce::Slider::thumbColourId, goldAccent);
        s.onValueChange = [&s, onChange] { onChange(s.getValue()); };
        addAndMakeVisible(s);

        l.setText(name, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, textMuted);
        addAndMakeVisible(l);
    };

    setupKnob(glideSlider, glideLabel, "GLIDE (s)", 0.0, 0.5, processor.engineState.glideTimeSec.load(),
              [this](double v) { processor.engineState.glideTimeSec.store(static_cast<float>(v)); });

    setupKnob(attackSlider, attackLabel, "ATTACK (s)", 0.002, 1.0, processor.engineState.attackSec.load(),
              [this](double v) { processor.engineState.attackSec.store(static_cast<float>(v)); });

    setupKnob(releaseSlider, releaseLabel, "RELEASE (s)", 0.01, 2.0, processor.engineState.releaseSec.load(),
              [this](double v) { processor.engineState.releaseSec.store(static_cast<float>(v)); });

    setupKnob(gainSlider, gainLabel, "GAIN", 0.0, 1.5, processor.engineState.masterGain.load(),
              [this](double v) { processor.engineState.masterGain.store(static_cast<float>(v)); });

    // Pitch Bend Range
    pbLabel.setText("PITCH BEND", juce::dontSendNotification);
    pbLabel.setColour(juce::Label::textColourId, textMuted);
    addAndMakeVisible(pbLabel);

    pbRangeBox.addItem("+/- 2 Semitones", 2);
    pbRangeBox.addItem("+/- 12 Semitones", 12);
    pbRangeBox.setSelectedId(processor.engineState.pitchBendRange.load(), juce::dontSendNotification);
    pbRangeBox.onChange = [this] {
        processor.engineState.pitchBendRange.store(pbRangeBox.getSelectedId());
    };
    addAndMakeVisible(pbRangeBox);

    // On-screen Keyboard
    midiKeyboard.setAvailableRange(36, 96);
    addAndMakeVisible(midiKeyboard);
}

void KorgKMPEngineEditor::applyMaqamPreset(const std::array<float, 12>& cents) {
    for (int i = 0; i < 12; ++i)
        processor.engineState.microtuneCents[i].store(cents[i]);
    refreshQuarterToneButtons();
}

void KorgKMPEngineEditor::refreshQuarterToneButtons() {
    for (int i = 0; i < 12; ++i) {
        bool isQuarter = processor.engineState.microtuneCents[i].load() < -10.0f;
        noteTuneButtons[i].setColour(juce::TextButton::buttonColourId, isQuarter ? goldAccent : panelDark);
        noteTuneButtons[i].setColour(juce::TextButton::textColourOffId, isQuarter ? juce::Colours::black : textLight);
    }
}

void KorgKMPEngineEditor::paint(juce::Graphics& g) {
    g.fillAll(bgDark);

    // Top Header Bar
    g.setColour(panelDark);
    g.fillRoundedRectangle(12.0f, 12.0f, getWidth() - 24.0f, 56.0f, 6.0f);
    g.setColour(goldAccent.withAlpha(0.4f));
    g.drawRoundedRectangle(12.0f, 12.0f, getWidth() - 24.0f, 56.0f, 6.0f, 1.0f);

    // Scale Panel
    g.setColour(panelDark);
    g.fillRoundedRectangle(12.0f, 80.0f, getWidth() - 24.0f, 92.0f, 6.0f);

    g.setColour(textMuted);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("ORIENTAL QUARTER-TONE SCALE (-50 CENTS)", 24, 86, 350, 20, juce::Justification::left);

    // Controls Panel
    g.setColour(panelDark);
    g.fillRoundedRectangle(12.0f, 184.0f, getWidth() - 24.0f, 136.0f, 6.0f);
}

void KorgKMPEngineEditor::resized() {
    titleLabel.setBounds(24, 16, 380, 24);
    statusLabel.setBounds(24, 40, 480, 22);
    loadButton.setBounds(getWidth() - 150, 22, 126, 36);

    // Presets row
    int px = getWidth() - 340;
    presetClear.setBounds(px, 86, 72, 22);
    presetBayatiD.setBounds(px + 78, 86, 78, 22);
    presetRastC.setBounds(px + 162, 86, 72, 22);
    presetSabaD.setBounds(px + 240, 86, 76, 22);

    // 12 Note Buttons
    int btnW = (getWidth() - 48 - (11 * 6)) / 12;
    for (int i = 0; i < 12; ++i) {
        noteTuneButtons[i].setBounds(24 + i * (btnW + 6), 116, btnW, 44);
    }

    // Knobs & Mode
    monoToggle.setBounds(28, 202, 150, 28);
    pbLabel.setBounds(28, 242, 140, 20);
    pbRangeBox.setBounds(28, 264, 145, 28);

    int knobStartX = 210;
    int knobSpacing = 132;
    auto layoutKnob = [](juce::Label& l, juce::Slider& s, int x) {
        l.setBounds(x, 192, 100, 18);
        s.setBounds(x, 212, 100, 96);
    };

    layoutKnob(glideLabel, glideSlider, knobStartX);
    layoutKnob(attackLabel, attackSlider, knobStartX + knobSpacing);
    layoutKnob(releaseLabel, releaseSlider, knobStartX + knobSpacing * 2);
    layoutKnob(gainLabel, gainSlider, knobStartX + knobSpacing * 3);

    midiKeyboard.setBounds(12, 332, getWidth() - 24, 96);
}
