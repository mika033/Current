#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Phase 1 processor: no parameters (nothing on the canvas is user-
// adjustable yet, per requirements doc "Phase 1: Canvas skeleton"), and no
// generation/processing logic (Phase 1 is the visual/interaction skeleton -
// modules exist as canvas boxes with fixed-default data, not a running
// signal graph). MIDI passes through untouched until that graph exists.
class CurrentAudioProcessor : public juce::AudioProcessor
{
public:
    CurrentAudioProcessor();
    ~CurrentAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override;

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurrentAudioProcessor)
};
