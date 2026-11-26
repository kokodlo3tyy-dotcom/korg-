#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "KorgParser.h"
#include <array>
#include <atomic>
#include <cmath>

struct OrientalEngineState {
    std::array<std::atomic<float>, 12> microtuneCents; // -50.0f or 0.0f
    std::atomic<bool> monoMode { true };
    std::atomic<float> glideTimeSec { 0.04f };
    std::atomic<float> attackSec { 0.01f };
    std::atomic<float> releaseSec { 0.15f };
    std::atomic<int> pitchBendRange { 2 };
    std::atomic<float> masterGain { 0.85f };

    OrientalEngineState() {
        for (auto& m : microtuneCents) m.store(0.0f);
    }
};

class KorgSamplerSound : public juce::SynthesiserSound {
public:
    explicit KorgSamplerSound(KorgSampleZone&& z) : zone(std::move(z)) {}

    bool appliesToNote(int midiNoteNumber) override {
        return midiNoteNumber >= zone.lowNote && midiNoteNumber <= zone.highNote;
    }

    bool appliesToChannel(int) override { return true; }

    const KorgSampleZone zone;
};

class KorgSamplerVoice : public juce::SynthesiserVoice {
public:
    explicit KorgSamplerVoice(OrientalEngineState& s) : state(s) {}

    bool canPlaySound(juce::SynthesiserSound* sound) override {
        return dynamic_cast<KorgSamplerSound*>(sound) != nullptr;
    }

    void startNote(int midiNoteNumber, float velocity,
                   juce::SynthesiserSound* sound, int currentPitchWheelPosition) override {
        auto* korgSound = static_cast<KorgSamplerSound*>(sound);
        if (korgSound == nullptr) return;

        bool wasPlaying = isVoiceActive() && adsr.isActive();
        currentSound = korgSound;
        noteVelocity = velocity;
        updatePitchWheel(currentPitchWheelPosition);

        adsrParams.attack = juce::jmax(0.002f, state.attackSec.load());
        adsrParams.decay = 0.1f;
        adsrParams.sustain = 1.0f;
        adsrParams.release = juce::jmax(0.01f, state.releaseSec.load());
        adsr.setSampleRate(getSampleRate());
        adsr.setParameters(adsrParams);

        if (state.monoMode.load() && wasPlaying) {
            // Legato / Glide transition without retriggering from zero
            noteSmoothed.reset(getSampleRate(), state.glideTimeSec.load());
            noteSmoothed.setTargetValue(static_cast<double>(midiNoteNumber));
        } else {
            sourceSamplePosition = 0.0;
            noteSmoothed.reset(getSampleRate(), 0.001);
            noteSmoothed.setCurrentAndTargetValue(static_cast<double>(midiNoteNumber));
            adsr.noteOn();
        }
    }

    void stopNote(float, bool allowTailOff) override {
        if (allowTailOff) {
            adsr.noteOff();
        } else {
            clearCurrentNote();
            adsr.reset();
            currentSound = nullptr;
        }
    }

    void pitchWheelMoved(int newPitchWheelValue) override {
        updatePitchWheel(newPitchWheelValue);
    }

    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override {
        if (currentSound == nullptr) return;

        const auto& zone = currentSound->zone;
        const float* inData = zone.buffer.getReadPointer(0);
        const int bufferLen = zone.buffer.getNumSamples();
        if (bufferLen <= 1) return;

        const double srRatio = zone.sampleRate / getSampleRate();
        const float gain = noteVelocity * zone.gainLinear * state.masterGain.load();
        const int pbRange = state.pitchBendRange.load();

        for (int i = 0; i < numSamples; ++i) {
            if (!adsr.isActive()) {
                clearCurrentNote();
                currentSound = nullptr;
                break;
            }

            double currentNote = noteSmoothed.getNextValue();
            int pitchClass = static_cast<int>(std::round(currentNote)) % 12;
            if (pitchClass < 0) pitchClass += 12;

            double microSemis = state.microtuneCents[pitchClass].load() / 100.0;
            double zoneTuneSemis = zone.tuneCents / 100.0;
            double semitoneOffset = (currentNote - zone.rootNote)
                                  + microSemis
                                  + zoneTuneSemis
                                  + (pitchBendNorm * pbRange);

            double pitchRatio = std::pow(2.0, semitoneOffset / 12.0) * srRatio;

            int pos0 = static_cast<int>(sourceSamplePosition);
            if (pos0 >= bufferLen - 1) {
                if (zone.hasLoop && zone.loopEnd > zone.loopStart) {
                    sourceSamplePosition = static_cast<double>(zone.loopStart);
                    pos0 = zone.loopStart;
                } else {
                    clearCurrentNote();
                    currentSound = nullptr;
                    break;
                }
            }

            int pos1 = pos0 + 1;
            if (pos1 >= zone.loopEnd && zone.hasLoop)
                pos1 = zone.loopStart;
            else if (pos1 >= bufferLen)
                pos1 = bufferLen - 1;

            float frac = static_cast<float>(sourceSamplePosition - pos0);
            float sample = inData[pos0] + frac * (inData[pos1] - inData[pos0]);
            float env = adsr.getNextSample();
            float outSample = sample * env * gain;

            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch) {
                outputBuffer.addSample(ch, startSample + i, outSample);
            }

            sourceSamplePosition += pitchRatio;
            if (zone.hasLoop && zone.loopEnd > zone.loopStart && sourceSamplePosition >= zone.loopEnd) {
                sourceSamplePosition = zone.loopStart + std::fmod(sourceSamplePosition - zone.loopStart,
                                                                  static_cast<double>(zone.loopEnd - zone.loopStart));
            }
        }
    }

private:
    void updatePitchWheel(int wheelPos) {
        pitchBendNorm = (wheelPos - 8192) / 8192.0;
    }

    OrientalEngineState& state;
    const KorgSamplerSound* currentSound = nullptr;
    double sourceSamplePosition = 0.0;
    double pitchBendNorm = 0.0;
    float noteVelocity = 1.0f;
    juce::SmoothedValue<double> noteSmoothed;
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;
};
