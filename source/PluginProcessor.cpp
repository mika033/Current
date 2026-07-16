#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

namespace
{
    const juce::StringArray kRootNames { "C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B" };

    // A representative handful for Phase 2; the full scale set arrives with the
    // Quantize module's real settings in a later phase.
    const juce::StringArray kScaleNames { "Major", "Minor", "Dorian", "Phrygian",
                                          "Lydian", "Mixolydian", "Locrian",
                                          "Pentatonic", "Chromatic" };

    const juce::StringArray kThemeNames { "Light", "Dark" };
}

CurrentAudioProcessor::CurrentAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          // A stereo bus we never fill with audio: some VST3 hosts (Live) reject
          // an effect plugin that exposes no audio bus, even a MIDI effect.
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "STATE", createLayout())
{
    rootParam     = parameters.getRawParameterValue (ParamIDs::root);
    scaleParam    = parameters.getRawParameterValue (ParamIDs::scale);
    quantizeParam = parameters.getRawParameterValue (ParamIDs::quantize);
}

CurrentAudioProcessor::~CurrentAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout CurrentAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::root, 1 }, "Root", kRootNames, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::scale, 1 }, "Scale", kScaleNames, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::quantize, 1 }, "Quantize", true));

    // Default index 1 = Dark, matching CurrentTheme::gActive's default.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::theme, 1 }, "Theme", kThemeNames, 1));

    return layout;
}

void CurrentAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
}

void CurrentAudioProcessor::releaseResources() {}

bool CurrentAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Accept the disabled bus or a mono/stereo match — we don't touch audio, so
    // we just need the host's layout negotiation to succeed.
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::disabled()
        && out != juce::AudioChannelSet::mono()
        && out != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void CurrentAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // We produce no audio — clear whatever the host handed us.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    Engine::Config cfg;
    cfg.hasArp      = engHasArp.load();
    cfg.hasRandom   = engHasRandom.load();
    cfg.hasQuantize = engHasQuantize.load();
    cfg.hasShift    = engHasShift.load();

    const int  root          = (int) (rootParam     != nullptr ? rootParam->load()  : 0.0f);
    const int  scaleIndex    = (int) (scaleParam    != nullptr ? scaleParam->load() : 0.0f);
    const bool globalQuantize = (quantizeParam != nullptr ? quantizeParam->load() > 0.5f : false);

    juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
    if (auto* ph = getPlayHead())
        pos = ph->getPosition();

    engine.process (midi, buffer.getNumSamples(), pos,
                    root, scaleIndex, globalQuantize, cfg);
}

juce::AudioProcessorEditor* CurrentAudioProcessor::createEditor()
{
    return new CurrentAudioProcessorEditor (*this);
}

// --- Canvas model -----------------------------------------------------------

void CurrentAudioProcessor::refreshEngineConfig()
{
    bool arp = false, rnd = false, quant = false, shift = false;
    for (const auto& m : moduleList)
    {
        switch (m.type)
        {
            case ModuleType::Arp:      arp   = true; break;
            case ModuleType::Random:   rnd   = true; break;
            case ModuleType::Quantize: quant = true; break;
            case ModuleType::Shift:    shift = true; break;
        }
    }
    engHasArp.store (arp);
    engHasRandom.store (rnd);
    engHasQuantize.store (quant);
    engHasShift.store (shift);
}

int CurrentAudioProcessor::addModule (ModuleType type, float x, float y)
{
    ModuleInstance m;
    m.id   = nextModuleId++;
    m.type = type;
    m.x    = x;
    m.y    = y;
    moduleList.push_back (m);
    refreshEngineConfig();
    return m.id;
}

void CurrentAudioProcessor::moveModule (int id, float x, float y)
{
    for (auto& m : moduleList)
    {
        if (m.id == id)
        {
            m.x = x;
            m.y = y;
            return;
        }
    }
}

void CurrentAudioProcessor::removeModule (int id)
{
    moduleList.erase (std::remove_if (moduleList.begin(), moduleList.end(),
                                      [id] (const ModuleInstance& m) { return m.id == id; }),
                      moduleList.end());
    refreshEngineConfig();
}

// --- State ------------------------------------------------------------------

void CurrentAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Root of the saved state is the APVTS tree; the canvas layout rides along
    // as a child node. This is DAW-project persistence, not the user-facing
    // Load/Save of patches (which Phase 2 deliberately omits).
    auto state = parameters.copyState();

    juce::ValueTree canvas ("Canvas");
    for (const auto& m : moduleList)
    {
        juce::ValueTree node ("Module");
        node.setProperty ("type", moduleTypeToString (m.type), nullptr);
        node.setProperty ("x", m.x, nullptr);
        node.setProperty ("y", m.y, nullptr);
        canvas.appendChild (node, nullptr);
    }
    state.appendChild (canvas, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CurrentAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid())
        return;

    moduleList.clear();
    nextModuleId = 1;

    if (auto canvas = state.getChildWithName ("Canvas"); canvas.isValid())
    {
        for (const auto& node : canvas)
        {
            ModuleInstance m;
            m.id   = nextModuleId++;
            m.type = moduleTypeFromString (node.getProperty ("type").toString());
            m.x    = (float) node.getProperty ("x");
            m.y    = (float) node.getProperty ("y");
            moduleList.push_back (m);
        }
        state.removeChild (canvas, nullptr);
    }

    refreshEngineConfig();
    parameters.replaceState (state);

    // Async + thread-safe, so it's fine even if a host calls
    // setStateInformation off the message thread.
    canvasModelReplaced.sendChangeMessage();
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CurrentAudioProcessor();
}
