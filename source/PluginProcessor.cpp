#include "PluginProcessor.h"
#include "PluginEditor.h"

CurrentAudioProcessor::CurrentAudioProcessor()
    : AudioProcessor (BusesProperties())
{
}

void CurrentAudioProcessor::prepareToPlay (double, int)
{
}

void CurrentAudioProcessor::releaseResources()
{
}

void CurrentAudioProcessor::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    // No signal graph yet - Phase 1 is the canvas skeleton, not the engine.
}

juce::AudioProcessorEditor* CurrentAudioProcessor::createEditor()
{
    return new CurrentAudioProcessorEditor (*this);
}

bool CurrentAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String CurrentAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool CurrentAudioProcessor::acceptsMidi() const
{
    return true;
}

bool CurrentAudioProcessor::producesMidi() const
{
    return true;
}

bool CurrentAudioProcessor::isMidiEffect() const
{
    return true;
}

double CurrentAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int CurrentAudioProcessor::getNumPrograms()
{
    return 1;
}

int CurrentAudioProcessor::getCurrentProgram()
{
    return 0;
}

void CurrentAudioProcessor::setCurrentProgram (int)
{
}

const juce::String CurrentAudioProcessor::getProgramName (int)
{
    return {};
}

void CurrentAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void CurrentAudioProcessor::getStateInformation (juce::MemoryBlock&)
{
    // No state yet - Phase 1 has no user-adjustable settings and no Load/Save.
}

void CurrentAudioProcessor::setStateInformation (const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CurrentAudioProcessor();
}
