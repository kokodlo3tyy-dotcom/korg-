#include "PluginEditor.h"

namespace {
    const juce::Colour bgDark     { 0xff121316 };
    const juce::Colour panelDark  { 0xff1b1d22 };
    const juce::Colour rowAltDark { 0xff16181c };
    const juce::Colour goldAccent { 0xffd4af37 };
    const juce::Colour textLight  { 0xfff0f0f0 };
    const juce::Colour textMuted  { 0xff8c929d };
}

KorgKMPEngineEditor::KorgKMPEngineEditor(KorgKMPEngineProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      midiKeyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {

    // --- Left Sidebar: Sound Browser ---
    browserTitleLabel.setText("SOUND BROWSER", juce::dontSendNotification);
    browserTitleLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    browserTitleLabel.setColour(juce::Label::textColourId, goldAccent);
    addAndMakeVisible(browserTitleLabel);

    folderNameLabel.setText(processor.currentFolderName, juce::dontSendNotification);
    folderNameLabel.setFont(juce::FontOptions(12.0f));
    folderNameLabel.setColour(juce::Label::textColourId, textMuted);
    addAndMakeVisible(folderNameLabel);

    openFolderButton.setColour(juce::TextButton::buttonColourId, goldAccent);
    openFolderButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    openFolderButton.onClick = [this] {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select Korg Set or Multisample Folder",
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
        );
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
            auto dir = fc.getResult();
            if (dir.isDirectory()) {
                processor.scanFolderForKMP(dir);
                syncBrowserAndStatus();
            }
        });
    };
    addAndMakeVisible(openFolderButton);

    auto styleNavBtn = [this](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, bgDark);
        btn.setColour(juce::TextButton::textColourOffId, goldAccent);
        addAndMakeVisible(btn);
    };
    styleNavBtn(prevSoundButton);
    styleNavBtn(nextSoundButton);

    prevSoundButton.onClick = [this] {
        processor.selectPreviousSound();
        syncBrowserAndStatus();
    };
    nextSoundButton.onClick = [this] {
        processor.selectNextSound();
        syncBrowserAndStatus();
    };

    soundListBox.setModel(this);
    soundListBox.setRowHeight(28);
    soundListBox.setColour(juce::ListBox::backgroundColourId, bgDark);
    soundListBox.setColour(juce::ListBox::outlineColourId, goldAccent.withAlpha(0.25f));
    soundListBox.setOutlineThickness(1);
    addAndMakeVisible(soundListBox);

    // --- Header ---
    titleLabel.setText("MIDO MAZZIKA // KORG KMP ENGINE", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, goldAccent);
    addAndMakeVisible(titleLabel);

    statusLabel.setFont(juce::FontOptions(14.0f));
    statusLabel.setColour(juce::Label::textColourId, textLight);
    addAndMakeVisible(statusLabel);

    loadButton.setColour(juce::TextButton::buttonColourId, panelDark);
    loadButton.setColour(juce::TextButton::textColourOffId, goldAccent);
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
                processor.loadSingleKMPWithSiblings(file);
                syncBrowserAndStatus();
            }
        });
    };
    addAndMakeVisible(loadButton);

    // --- 12-Note Quarter-Tone Strip ---
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
        btn.setColour(juce::TextButton::buttonColourId, bgDark);
        btn.setColour(juce::TextButton::textColourOffId, goldAccent);
        btn.onClick = [this, map] { applyMaqamPreset(map); };
        addAndMakeVisible(btn);
    };

    setupPresetBtn(presetClear,   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
    setupPresetBtn(presetBayatiD, { 0, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -50 });
    setupPresetBtn(presetRastC,   { 0, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -50 });
    setupPresetBtn(presetSabaD,   { 0, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, 0 });

    refreshQuarterToneButtons();

    // --- Mono / Legato Toggle ---
    monoToggle.setToggleState(processor.engineState.monoMode.load(), juce::dontSendNotification);
    monoToggle.setColour(juce::ToggleButton::textColourId, textLight);
    monoToggle.setColour(juce::ToggleButton::tickColourId, goldAccent);
    monoToggle.onClick = [this] {
        processor.engineState.monoMode.store(monoToggle.getToggleState());
        processor.updateVoiceCount();
    };
    addAndMakeVisible(monoToggle);

    // --- Rotary Knobs ---
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

    // --- Pitch Bend Range ---
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

    // --- On-screen Keyboard ---
    midiKeyboard.setAvailableRange(36, 96);
    addAndMakeVisible(midiKeyboard);

    syncBrowserAndStatus();
    setSize(980, 450);
}

int KorgKMPEngineEditor::getNumRows() {
    return static_cast<int>(processor.kmpFiles.size());
}

void KorgKMPEngineEditor::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(processor.kmpFiles.size()))
        return;

    if (rowIsSelected) {
        g.fillAll(goldAccent);
    } else {
        g.fillAll(rowNumber % 2 == 0 ? panelDark : rowAltDark);
    }

    juce::String indexStr = juce::String(rowNumber + 1).paddedLeft('0', 2) + ". ";
    juce::String nameStr = processor.kmpFiles[static_cast<size_t>(rowNumber)].getFileNameWithoutExtension();

    g.setFont(juce::FontOptions(13.0f, rowIsSelected ? juce::Font::bold : juce::Font::plain));
    g.setColour(rowIsSelected ? juce::Colours::black : goldAccent);
    g.drawText(indexStr, 8, 0, 28, height, juce::Justification::centredLeft, false);

    g.setColour(rowIsSelected ? juce::Colours::black : textLight);
    g.drawText(nameStr, 36, 0, width - 42, height, juce::Justification::centredLeft, true);
}

void KorgKMPEngineEditor::selectedRowsChanged(int lastRowSelected) {
    if (isUpdatingSelection) return;
    if (lastRowSelected >= 0 && lastRowSelected < static_cast<int>(processor.kmpFiles.size())) {
        processor.selectSoundByIndex(lastRowSelected);
        statusLabel.setText(processor.loadedInstrumentName + " (" + juce::String(processor.loadedZonesCount) + " Zones)",
                            juce::dontSendNotification);
    }
}

void KorgKMPEngineEditor::syncBrowserAndStatus() {
    isUpdatingSelection = true;
    folderNameLabel.setText(processor.currentFolderName + " (" + juce::String(processor.kmpFiles.size()) + ")",
                            juce::dontSendNotification);
    soundListBox.updateContent();

    if (processor.currentSoundIndex >= 0 && processor.currentSoundIndex < static_cast<int>(processor.kmpFiles.size())) {
        soundListBox.selectRow(processor.currentSoundIndex);
        soundListBox.scrollToEnsureRowIsOnscreen(processor.currentSoundIndex);
    }
    soundListBox.repaint();

    statusLabel.setText(processor.loadedInstrumentName + " (" + juce::String(processor.loadedZonesCount) + " Zones)",
                        juce::dontSendNotification);
    isUpdatingSelection = false;
}

void KorgKMPEngineEditor::applyMaqamPreset(const std::array<float, 12>& cents) {
    for (int i = 0; i < 12; ++i)
        processor.engineState.microtuneCents[i].store(cents[i]);
    refreshQuarterToneButtons();
}

void KorgKMPEngineEditor::refreshQuarterToneButtons() {
    for (int i = 0; i < 12; ++i) {
        bool isQuarter = processor.engineState.microtuneCents[i].load() < -10.0f;
        noteTuneButtons[i].setColour(juce::TextButton::buttonColourId, isQuarter ? goldAccent : bgDark);
        noteTuneButtons[i].setColour(juce::TextButton::textColourOffId, isQuarter ? juce::Colours::black : textLight);
    }
}

void KorgKMPEngineEditor::paint(juce::Graphics& g) {
    g.fillAll(bgDark);

    // Left Sidebar Panel
    g.setColour(panelDark);
    g.fillRoundedRectangle(12.0f, 12.0f, 232.0f, getHeight() - 24.0f, 6.0f);
    g.setColour(goldAccent.withAlpha(0.3f));
    g.drawRoundedRectangle(12.0f, 12.0f, 232.0f, getHeight() - 24.0f, 6.0f, 1.0f);

    const float mainX = 256.0f;
    const float mainW = getWidth() - mainX - 12.0f;

    // Top Header Bar
    g.setColour(panelDark);
    g.fillRoundedRectangle(mainX, 12.0f, mainW, 56.0f, 6.0f);
    g.setColour(goldAccent.withAlpha(0.4f));
    g.drawRoundedRectangle(mainX, 12.0f, mainW, 56.0f, 6.0f, 1.0f);

    // Scale Panel
    g.setColour(panelDark);
    g.fillRoundedRectangle(mainX, 80.0f, mainW, 92.0f, 6.0f);

    g.setColour(textMuted);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("ORIENTAL QUARTER-TONE SCALE (-50 CENTS)", static_cast<int>(mainX + 12), 86, 350, 20, juce::Justification::left);

    // Controls Panel
    g.setColour(panelDark);
    g.fillRoundedRectangle(mainX, 184.0f, mainW, 136.0f, 6.0f);
}

void KorgKMPEngineEditor::resized() {
    // Sidebar layout
    browserTitleLabel.setBounds(20, 16, 216, 20);
    openFolderButton.setBounds(20, 40, 148, 28);
    prevSoundButton.setBounds(174, 40, 28, 28);
    nextSoundButton.setBounds(206, 40, 28, 28);
    folderNameLabel.setBounds(20, 72, 216, 18);
    soundListBox.setBounds(20, 94, 216, getHeight() - 114);

    // Right main area
    const int mainX = 256;
    const int mainW = getWidth() - mainX - 12;

    titleLabel.setBounds(mainX + 12, 16, 380, 24);
    statusLabel.setBounds(mainX + 12, 40, 450, 22);
    loadButton.setBounds(getWidth() - 140, 22, 116, 36);

    // Presets row
    int px = getWidth() - 340;
    presetClear.setBounds(px, 86, 72, 22);
    presetBayatiD.setBounds(px + 78, 86, 78, 22);
    presetRastC.setBounds(px + 162, 86, 72, 22);
    presetSabaD.setBounds(px + 240, 86, 76, 22);

    // 12 Note Buttons
    int btnW = (mainW - 24 - (11 * 6)) / 12;
    for (int i = 0; i < 12; ++i) {
        noteTuneButtons[i].setBounds(mainX + 12 + i * (btnW + 6), 116, btnW, 44);
    }

    // Knobs & Mode
    monoToggle.setBounds(mainX + 16, 202, 150, 28);
    pbLabel.setBounds(mainX + 16, 242, 140, 20);
    pbRangeBox.setBounds(mainX + 16, 264, 145, 28);

    int knobStartX = mainX + 190;
    int knobSpacing = 126;
    auto layoutKnob = [](juce::Label& l, juce::Slider& s, int x) {
        l.setBounds(x, 192, 98, 18);
        s.setBounds(x, 212, 98, 96);
    };

    layoutKnob(glideLabel, glideSlider, knobStartX);
    layoutKnob(attackLabel, attackSlider, knobStartX + knobSpacing);
    layoutKnob(releaseLabel, releaseSlider, knobStartX + knobSpacing * 2);
    layoutKnob(gainLabel, gainSlider, knobStartX + knobSpacing * 3);

    midiKeyboard.setBounds(mainX, 332, mainW, 106);
}
