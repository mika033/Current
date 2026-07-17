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
    cfg.hasArp          = engHasArp.load();
    cfg.hasRandom       = engHasRandom.load();
    cfg.hasScaleGen     = engHasScaleGen.load();
    cfg.hasLfo          = engHasLfo.load();
    cfg.hasQuantize     = engHasQuantize.load();
    cfg.hasShift        = engHasShift.load();
    cfg.hasDelay        = engHasDelay.load();
    cfg.hasMidiIn       = engHasMidiIn.load();
    cfg.hasOutput       = engHasOutput.load();
    cfg.inChannelMask   = engInChannelMask.load();
    cfg.outChannelMask  = engOutChannelMask.load();

    cfg.arpMode     = engArpMode.load();
    cfg.arpStepQn   = ModuleOptions::rateQuarterNotes (engArpRate.load());
    cfg.arpOctaves  = engArpOctaves.load();
    cfg.arpGateFrac = ModuleOptions::gateFraction (engArpGate.load());
    cfg.arpRepeatQn = ModuleOptions::repeatQuarterNotes (engArpRepeat.load());

    cfg.randomRoot   = engRandomRoot.load();
    cfg.randomScale  = engRandomScale.load();
    cfg.randomStepQn = ModuleOptions::rateQuarterNotes (engRandomRate.load());
    cfg.randomFrom   = engRandomFrom.load();
    cfg.randomTo     = engRandomTo.load();

    cfg.scaleRoot      = engScaleRoot.load();
    cfg.scaleScale     = engScaleScale.load();
    cfg.scaleStepQn    = ModuleOptions::rateQuarterNotes (engScaleRate.load());
    cfg.scaleRepeatQn  = ModuleOptions::repeatQuarterNotes (engScaleRepeat.load());
    cfg.scaleOctaves   = engScaleOctaves.load();
    cfg.scaleMode      = engScaleMode.load();
    cfg.scaleEndOnRoot = engScaleEndOnRoot.load();

    cfg.lfoRoot       = engLfoRoot.load();
    cfg.lfoScale      = engLfoScale.load();
    cfg.lfoStepQn     = ModuleOptions::rateQuarterNotes (engLfoRate.load());
    cfg.lfoCycleQn    = ModuleOptions::lfoCycleQuarterNotes (engLfoCycle.load());
    cfg.lfoShape      = engLfoShape.load();
    cfg.lfoDepthOct   = engLfoDepthOct.load();
    cfg.lfoDepthSteps = engLfoDepthSteps.load();
    cfg.lfoPhase      = ModuleOptions::lfoPhaseFraction (engLfoPhase.load());

    cfg.shiftAmount = engShiftAmount.load();
    cfg.shiftScale  = engShiftScale.load();

    cfg.delayTimeQn   = ModuleOptions::rateQuarterNotes (engDelayRate.load());
    cfg.delayFeedback = ModuleOptions::feedbackFraction (engDelayFeedback.load());
    cfg.delayShift    = engDelayShift.load();

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
    bool arp = false, rnd = false, scaleGen = false, lfo = false;
    bool quant = false, shift = false, delay = false;
    bool midiIn = false, output = false;
    std::uint16_t inMask = 0, outMask = 0;

    for (const auto& m : moduleList)
    {
        switch (m.type)
        {
            case ModuleType::Arp:
                // First instance's settings win — the implicit chain runs one
                // Arp at most until wiring lands. Same below for Random/Scale.
                if (! arp)
                {
                    engArpMode.store (m.settings.mode);
                    engArpRate.store (m.settings.rate);
                    engArpOctaves.store (m.settings.octaves);
                    engArpGate.store (m.settings.gate);
                    engArpRepeat.store (m.settings.repeat);
                }
                arp = true;
                break;
            case ModuleType::Random:
                if (! rnd)
                {
                    engRandomRoot.store (m.settings.rootOverride);
                    engRandomScale.store (m.settings.scaleOverride);
                    engRandomRate.store (m.settings.rate);
                    engRandomFrom.store (m.settings.rangeFrom);
                    engRandomTo.store (m.settings.rangeTo);
                }
                rnd = true;
                break;
            case ModuleType::ScaleGen:
                if (! scaleGen)
                {
                    engScaleRoot.store (m.settings.rootOverride);
                    engScaleScale.store (m.settings.scaleOverride);
                    engScaleRate.store (m.settings.rate);
                    engScaleRepeat.store (m.settings.repeat);
                    engScaleOctaves.store (m.settings.octaves);
                    engScaleMode.store (m.settings.mode);
                    engScaleEndOnRoot.store (m.settings.endOnRoot);
                }
                scaleGen = true;
                break;
            case ModuleType::Lfo:
                if (! lfo)
                {
                    engLfoRoot.store (m.settings.rootOverride);
                    engLfoScale.store (m.settings.scaleOverride);
                    engLfoRate.store (m.settings.rate);
                    engLfoCycle.store (m.settings.lfoCycle);
                    engLfoShape.store (m.settings.lfoShape);
                    engLfoDepthOct.store (m.settings.lfoDepthOct);
                    engLfoDepthSteps.store (m.settings.lfoDepthSteps);
                    engLfoPhase.store (m.settings.lfoPhase);
                }
                lfo = true;
                break;
            case ModuleType::Quantize: quant = true; break;
            case ModuleType::Shift:
                if (! shift)
                {
                    engShiftAmount.store (m.settings.shiftAmount);
                    engShiftScale.store (m.settings.scaleOverride);
                }
                shift = true;
                break;
            case ModuleType::Delay:
                if (! delay)
                {
                    engDelayRate.store (m.settings.rate);
                    engDelayFeedback.store (m.settings.delayFeedback);
                    engDelayShift.store (m.settings.delayShift);
                }
                delay = true;
                break;
            case ModuleType::MidiIn:
                midiIn = true;
                // Channel 0 = All; several MIDI Ins merge (union).
                inMask |= (m.channel == 0)
                              ? (std::uint16_t) 0xffff
                              : (std::uint16_t) (1u << (juce::jlimit (1, 16, m.channel) - 1));
                break;
            case ModuleType::Output:
                output = true;
                outMask |= (std::uint16_t) (1u << (juce::jlimit (1, 16, m.channel) - 1));
                break;
        }
    }

    engHasArp.store (arp);
    engHasRandom.store (rnd);
    engHasScaleGen.store (scaleGen);
    engHasLfo.store (lfo);
    engHasQuantize.store (quant);
    engHasShift.store (shift);
    engHasDelay.store (delay);
    engHasMidiIn.store (midiIn);
    engHasOutput.store (output);
    // No MIDI In module = implicit all-channels input; no Output = keep each
    // event's own channel (mask 0 means exactly that in the engine).
    engInChannelMask.store (midiIn ? inMask : (std::uint16_t) 0xffff);
    engOutChannelMask.store (output ? outMask : (std::uint16_t) 0);
}

int CurrentAudioProcessor::addModule (ModuleType type, float x, float y)
{
    ModuleInstance m;
    m.id      = nextModuleId++;
    m.type    = type;
    m.x       = x;
    m.y       = y;
    m.channel = defaultChannelFor (type);

    if (type == ModuleType::Random)
    {
        // Default range per the requirements: root at octave 1 up to root at
        // octave 3 (e.g. C1..C3 = MIDI 24..48), taken from the global root at
        // drop time since the module's own root starts on Global.
        const int root = (int) (rootParam != nullptr ? rootParam->load() : 0.0f);
        m.settings.rangeFrom = juce::jlimit (0, 127, 24 + root);
        m.settings.rangeTo   = juce::jlimit (0, 127, 48 + root);
    }
    else if (type == ModuleType::ScaleGen)
    {
        // 1/8 steps + 1-bar repeat: the default one-octave pattern capped with
        // the octave root is 8 notes, which fills the bar exactly. (Repeat is
        // Endless everywhere else — this default lineup is the exception.)
        m.settings.rate   = ModuleOptions::kRate1_8;
        m.settings.repeat = ModuleOptions::kRepeatOneBar;
    }
    else if (type == ModuleType::Delay)
    {
        // 1/8 echoes: the shared rate default of 1/16 is generator-paced and
        // too fast to read as an echo.
        m.settings.rate = ModuleOptions::kRate1_8;
    }

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

void CurrentAudioProcessor::setModuleChannel (int id, int channel)
{
    for (auto& m : moduleList)
    {
        if (m.id == id)
        {
            m.channel = channel;
            refreshEngineConfig();
            return;
        }
    }
}

int CurrentAudioProcessor::getModuleChannel (int id) const
{
    for (const auto& m : moduleList)
        if (m.id == id)
            return m.channel;
    return 0;
}

void CurrentAudioProcessor::setModuleSettings (int id, const ModuleSettings& settings)
{
    for (auto& m : moduleList)
    {
        if (m.id == id)
        {
            m.settings = settings;
            refreshEngineConfig();
            return;
        }
    }
}

ModuleSettings CurrentAudioProcessor::getModuleSettings (int id) const
{
    for (const auto& m : moduleList)
        if (m.id == id)
            return m.settings;
    return {};
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
        // Only the I/O modules have a channel setting worth persisting.
        if (m.type == ModuleType::MidiIn || m.type == ModuleType::Output)
            node.setProperty ("channel", m.channel, nullptr);
        // The shared module settings, only where the type actually uses them.
        if (m.type == ModuleType::Random || m.type == ModuleType::ScaleGen
            || m.type == ModuleType::Lfo)
        {
            node.setProperty ("root",  m.settings.rootOverride, nullptr);
            node.setProperty ("scale", m.settings.scaleOverride, nullptr);
        }
        if (m.type == ModuleType::Random || m.type == ModuleType::ScaleGen
            || m.type == ModuleType::Arp || m.type == ModuleType::Lfo
            || m.type == ModuleType::Delay)
            node.setProperty ("rate", m.settings.rate, nullptr);
        if (m.type == ModuleType::ScaleGen || m.type == ModuleType::Arp)
        {
            node.setProperty ("mode",    m.settings.mode, nullptr);
            node.setProperty ("octaves", m.settings.octaves, nullptr);
            node.setProperty ("repeat",  m.settings.repeat, nullptr);
        }
        if (m.type == ModuleType::Random)
        {
            node.setProperty ("from", m.settings.rangeFrom, nullptr);
            node.setProperty ("to",   m.settings.rangeTo, nullptr);
        }
        if (m.type == ModuleType::ScaleGen)
            node.setProperty ("endOnRoot", m.settings.endOnRoot, nullptr);
        if (m.type == ModuleType::Arp)
            node.setProperty ("gate", m.settings.gate, nullptr);
        if (m.type == ModuleType::Shift)
        {
            // "scale" doubles as Shift's chromatic/degree switch (kScaleOff).
            node.setProperty ("scale",       m.settings.scaleOverride, nullptr);
            node.setProperty ("shiftAmount", m.settings.shiftAmount, nullptr);
        }
        if (m.type == ModuleType::Lfo)
        {
            node.setProperty ("lfoShape",      m.settings.lfoShape, nullptr);
            node.setProperty ("lfoCycle",      m.settings.lfoCycle, nullptr);
            node.setProperty ("lfoDepthOct",   m.settings.lfoDepthOct, nullptr);
            node.setProperty ("lfoDepthSteps", m.settings.lfoDepthSteps, nullptr);
            node.setProperty ("lfoPhase",      m.settings.lfoPhase, nullptr);
        }
        if (m.type == ModuleType::Delay)
        {
            node.setProperty ("feedback",   m.settings.delayFeedback, nullptr);
            node.setProperty ("delayShift", m.settings.delayShift, nullptr);
        }
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
            m.id      = nextModuleId++;
            m.type    = moduleTypeFromString (node.getProperty ("type").toString());
            m.x       = (float) node.getProperty ("x");
            m.y       = (float) node.getProperty ("y");
            m.channel = (int) node.getProperty ("channel", defaultChannelFor (m.type));

            // Missing properties (older saves, other types) keep the struct's
            // defaults via the fallback argument. Per-type defaults (Scale's
            // rate/repeat) are restated so an untouched module reloads as it
            // was dropped.
            ModuleSettings def;
            // Types whose drop-time rate default differs from the struct's
            // (see addModule) restate it here so an untouched module reloads
            // as it was dropped.
            const bool isScaleGen = m.type == ModuleType::ScaleGen;
            const bool isDelay    = m.type == ModuleType::Delay;
            m.settings.rootOverride  = (int)  node.getProperty ("root",  def.rootOverride);
            m.settings.scaleOverride = (int)  node.getProperty ("scale", def.scaleOverride);
            m.settings.rate          = (int)  node.getProperty ("rate",
                                          (isScaleGen || isDelay) ? ModuleOptions::kRate1_8
                                                                  : def.rate);
            m.settings.rangeFrom     = (int)  node.getProperty ("from", def.rangeFrom);
            m.settings.rangeTo       = (int)  node.getProperty ("to",   def.rangeTo);
            m.settings.octaves       = (int)  node.getProperty ("octaves", def.octaves);
            m.settings.endOnRoot     = (bool) node.getProperty ("endOnRoot", def.endOnRoot);
            m.settings.gate          = (int)  node.getProperty ("gate", def.gate);
            m.settings.mode          = (int)  node.getProperty ("mode", def.mode);
            m.settings.repeat        = (int)  node.getProperty ("repeat",
                                          isScaleGen ? ModuleOptions::kRepeatOneBar : def.repeat);
            m.settings.shiftAmount   = (int)  node.getProperty ("shiftAmount", def.shiftAmount);
            m.settings.lfoShape      = (int)  node.getProperty ("lfoShape", def.lfoShape);
            m.settings.lfoCycle      = (int)  node.getProperty ("lfoCycle", def.lfoCycle);
            m.settings.lfoDepthOct   = (int)  node.getProperty ("lfoDepthOct", def.lfoDepthOct);
            m.settings.lfoDepthSteps = (int)  node.getProperty ("lfoDepthSteps", def.lfoDepthSteps);
            m.settings.lfoPhase      = (int)  node.getProperty ("lfoPhase", def.lfoPhase);
            m.settings.delayFeedback = (int)  node.getProperty ("feedback", def.delayFeedback);
            m.settings.delayShift    = (int)  node.getProperty ("delayShift", def.delayShift);

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
