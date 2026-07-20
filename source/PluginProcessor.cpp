#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

namespace
{
    const juce::StringArray kRootNames { "C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B" };

    // A representative handful for Phase 2; growing the full scale set is a
    // later phase. Index order must match ScaleTables::intervalsForScale.
    const juce::StringArray kScaleNames { "Major", "Minor", "Dorian", "Phrygian",
                                          "Lydian", "Mixolydian", "Locrian",
                                          "Pentatonic", "Chromatic" };

    const juce::StringArray kThemeNames { "Light", "Dark" };

    // Progression steps in the lock-free snapshot: one int per step, degree in
    // the high part, octave biased into the low bits. 16 leaves headroom over
    // the 5 octave choices (-2..+2).
    int packProgStep (const ProgressionStep& s)
    {
        return s.degree * 16 + (s.octave + ModuleOptions::kProgOctaveRange);
    }

    ProgressionStep unpackProgStep (int packed)
    {
        return { packed / 16, packed % 16 - ModuleOptions::kProgOctaveRange };
    }

    // Persistence form of the step list: "degree:octave" pairs, e.g.
    // "0:0,3:0,4:-1". Compact, human-readable in the saved XML, and trivially
    // versionable — the plugin is pre-release, so no migration shims.
    juce::String progStepsToString (const std::vector<ProgressionStep>& steps)
    {
        juce::StringArray parts;
        for (const auto& s : steps)
            parts.add (juce::String (s.degree) + ":" + juce::String (s.octave));
        return parts.joinIntoString (",");
    }

    std::vector<ProgressionStep> progStepsFromString (const juce::String& text)
    {
        std::vector<ProgressionStep> steps;
        for (const auto& part : juce::StringArray::fromTokens (text, ",", ""))
        {
            ProgressionStep s;
            s.degree = juce::jlimit (0, ModuleOptions::degreeNames().size() - 1,
                                     part.upToFirstOccurrenceOf (":", false, false).getIntValue());
            s.octave = juce::jlimit (-ModuleOptions::kProgOctaveRange,
                                     ModuleOptions::kProgOctaveRange,
                                     part.fromFirstOccurrenceOf (":", false, false).getIntValue());
            steps.push_back (s);
            if ((int) steps.size() >= ModuleOptions::kMaxProgSteps)
                break;
        }
        if (steps.empty())
            steps.push_back ({});   // the list is never empty — default step I
        return steps;
    }
}

CurrentAudioProcessor::CurrentAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          // A stereo bus we never fill with audio: some VST3 hosts (Live) reject
          // an effect plugin that exposes no audio bus, even a MIDI effect.
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "STATE", createLayout())
{
    rootParam  = parameters.getRawParameterValue (ParamIDs::root);
    scaleParam = parameters.getRawParameterValue (ParamIDs::scale);
}

CurrentAudioProcessor::~CurrentAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout CurrentAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::root, 1 }, "Root", kRootNames, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::scale, 1 }, "Scale", kScaleNames, 0));

    // Default index 1 = Dark, matching CurrentTheme::gActive's default.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::theme, 1 }, "Theme", kThemeNames, 1));

    return layout;
}

void CurrentAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    internalQn = 0.0;
    // Cleared so a Play toggle already on at re-init gets a fresh off->on
    // edge (and with it the rewind to bar 1) on the first block.
    prevInternalPlay = false;
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
    cfg.hasChord        = engHasChord.load();
    cfg.hasDrone        = engHasDrone.load();
    cfg.hasQuantize     = engHasQuantize.load();
    cfg.hasScaleMod     = engHasScaleMod.load();
    cfg.hasProgression  = engHasProgression.load();
    cfg.hasShift        = engHasShift.load();
    cfg.hasMirror       = engHasMirror.load();
    cfg.hasHarmonizer   = engHasHarmonizer.load();
    cfg.hasDelay        = engHasDelay.load();
    cfg.hasStrum        = engHasStrum.load();
    cfg.hasMidiIn       = engHasMidiIn.load();
    cfg.hasOutput       = engHasOutput.load();
    cfg.inChannelMask   = engInChannelMask.load();
    cfg.outChannelMask  = engOutChannelMask.load();

    cfg.arpMode     = engArpMode.load();
    cfg.arpStepQn   = ModuleOptions::rateQuarterNotes (engArpRate.load());
    cfg.arpOctaves  = engArpOctaves.load();
    cfg.arpGateFrac = ModuleOptions::gateFraction (engArpGate.load());
    cfg.arpRepeatQn = ModuleOptions::repeatQuarterNotes (engArpRepeat.load());

    cfg.randomRoot     = engRandomRoot.load();
    cfg.randomScale    = engRandomScale.load();
    cfg.randomStepQn   = ModuleOptions::rateQuarterNotes (engRandomRate.load());
    cfg.randomGateFrac = ModuleOptions::gateFraction (engRandomGate.load());
    cfg.randomFrom     = engRandomFrom.load();
    cfg.randomTo       = engRandomTo.load();

    cfg.scaleRoot      = engScaleRoot.load();
    cfg.scaleScale     = engScaleScale.load();
    cfg.scaleStepQn    = ModuleOptions::rateQuarterNotes (engScaleRate.load());
    cfg.scaleGateFrac  = ModuleOptions::gateFraction (engScaleGate.load());
    cfg.scaleRepeatQn  = ModuleOptions::repeatQuarterNotes (engScaleRepeat.load());
    cfg.scaleOctaves   = engScaleOctaves.load();
    cfg.scaleMode      = engScaleMode.load();
    cfg.scaleEndOnRoot = engScaleEndOnRoot.load();

    cfg.lfoRoot       = engLfoRoot.load();
    cfg.lfoScale      = engLfoScale.load();
    cfg.lfoStepQn     = ModuleOptions::rateQuarterNotes (engLfoRate.load());
    cfg.lfoGateFrac   = ModuleOptions::gateFraction (engLfoGate.load());
    cfg.lfoCycleQn    = ModuleOptions::barLengthQuarterNotes (engLfoCycle.load());
    cfg.lfoShape      = engLfoShape.load();
    cfg.lfoDepthOct   = engLfoDepthOct.load();
    cfg.lfoDepthSteps = engLfoDepthSteps.load();
    cfg.lfoPhase      = ModuleOptions::lfoPhaseFraction (engLfoPhase.load());

    cfg.chordRoot      = engChordRoot.load();
    cfg.chordScale     = engChordScale.load();
    cfg.chordDegree    = engChordDegree.load();
    cfg.chordType      = engChordType.load();
    cfg.chordInversion = engChordInversion.load();
    cfg.chordLengthQn  = ModuleOptions::barLengthQuarterNotes (engChordLength.load());
    cfg.chordPeriodQn  = ModuleOptions::repeatQuarterNotes (engChordRepeat.load());

    cfg.droneRoot     = engDroneRoot.load();
    cfg.droneScale    = engDroneScale.load();
    cfg.droneVoicing  = engDroneVoicing.load();
    cfg.droneOctave   = engDroneOctave.load();
    cfg.droneLengthQn = ModuleOptions::barLengthQuarterNotes (engDroneLength.load());
    cfg.dronePeriodQn = ModuleOptions::repeatQuarterNotes (engDroneRepeat.load());

    cfg.quantStepQn = ModuleOptions::rateQuarterNotes (engQuantRate.load());
    cfg.quantSwing  = ModuleOptions::swingFraction (engQuantSwing.load());

    cfg.scaleModRoot  = engScaleModRoot.load();
    cfg.scaleModScale = engScaleModScale.load();

    cfg.progRoot      = engProgRoot.load();
    cfg.progScale     = engProgScale.load();
    cfg.progRateQn    = ModuleOptions::barLengthQuarterNotes (engProgRate.load());
    cfg.progStepCount = juce::jlimit (0, ModuleOptions::kMaxProgSteps, engProgCount.load());
    for (int i = 0; i < cfg.progStepCount; ++i)
    {
        const auto step = unpackProgStep (engProgSteps[(size_t) i].load());
        cfg.progDegrees[(size_t) i] = step.degree;
        cfg.progOctaves[(size_t) i] = step.octave;
    }

    cfg.shiftAmount = engShiftAmount.load();
    cfg.shiftScale  = engShiftScale.load();
    cfg.shiftRoot   = engShiftRoot.load();

    cfg.mirrorCenter = engMirrorCenter.load();
    cfg.mirrorLow    = engMirrorLow.load();
    cfg.mirrorHigh   = engMirrorHigh.load();
    cfg.mirrorBounds = engMirrorBounds.load();
    cfg.mirrorScale  = engMirrorScale.load();
    cfg.mirrorRoot   = engMirrorRoot.load();

    cfg.delayTimeQn   = ModuleOptions::rateQuarterNotes (engDelayRate.load());
    cfg.delayFeedback = ModuleOptions::feedbackFraction (engDelayFeedback.load());
    cfg.delayShift    = engDelayShift.load();
    cfg.delayScale    = engDelayScale.load();
    cfg.delayRoot     = engDelayRoot.load();

    cfg.harmType      = engHarmType.load();
    cfg.harmInversion = engHarmInversion.load();
    cfg.harmMode      = engHarmMode.load();
    cfg.harmScale     = engHarmScale.load();
    cfg.harmRoot      = engHarmRoot.load();

    cfg.strumSpreadSec = ModuleOptions::strumSpreadSeconds (engStrumSpread.load());
    cfg.strumMode      = engStrumMode.load();
    cfg.strumCurve     = engStrumCurve.load();
    cfg.strumVelTilt   = (double) engStrumVelTilt.load() / (double) ModuleOptions::kStrumVelTiltRange;
    cfg.strumJitter    = (double) engStrumJitter.load()  / (double) ModuleOptions::kStrumJitterSteps;
    cfg.strumRepeatQn  = ModuleOptions::repeatQuarterNotes (engStrumRepeat.load());

    cfg.hasHumanize     = engHasHumanize.load();
    cfg.humanizeStepQn  = ModuleOptions::rateQuarterNotes (engHumanizeRate.load());
    cfg.humanizeSwing   = ModuleOptions::swingFraction (engHumanizeSwing.load());
    cfg.humanizeLayback = ModuleOptions::swingFraction (engHumanizeLayback.load());
    cfg.humanizeAccent  = ModuleOptions::swingFraction (engHumanizeAccent.load());
    cfg.humanizeTimeJit = ModuleOptions::swingFraction (engHumanizeTimeJit.load());
    cfg.humanizeVelJit  = ModuleOptions::swingFraction (engHumanizeVelJit.load());
    cfg.humanizeLenJit  = ModuleOptions::swingFraction (engHumanizeLenJit.load());

    const int root       = (int) (rootParam  != nullptr ? rootParam->load()  : 0.0f);
    const int scaleIndex = (int) (scaleParam != nullptr ? scaleParam->load() : 0.0f);

    juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
    if (auto* ph = getPlayHead())
        pos = ph->getPosition();

    // Hosts the engine can't sync to get the internal transport instead (the
    // LAM approach). Two cases: the Standalone — its playhead reports
    // isPlaying == false forever, so the menu bar's Play toggle drives
    // transport and the manual Tempo always wins over whatever BPM its
    // playhead claims — and a plugin host with no playhead at all, which
    // free-runs at the internal tempo. Ppq accumulates across blocks and
    // rewinds on every off->on edge so playback always starts at the top of
    // bar 1; the edge is detected here on the audio thread because
    // internalQn is a bare double touched by processBlock — a UI-thread
    // write would race. (A host playhead that merely lacks a ppq or bpm
    // value keeps its isPlaying and is patched per-field inside the engine.)
    if (isStandalone() || ! pos.hasValue())
    {
        const bool play = isStandalone() ? standalonePlay.load (std::memory_order_acquire)
                                         : true;
        const double bpm = internalBpm.load (std::memory_order_relaxed);
        if (play && ! prevInternalPlay)
            internalQn = 0.0;
        prevInternalPlay = play;

        juce::AudioPlayHead::PositionInfo pi;
        pi.setIsPlaying (play);
        pi.setBpm (bpm);
        pi.setPpqPosition (internalQn);
        if (play)
            internalQn += (double) buffer.getNumSamples()
                              / juce::jmax (1.0, (60.0 / bpm) * getSampleRate());
        pos = pi;
    }

    engine.process (midi, buffer.getNumSamples(), pos,
                    root, scaleIndex, cfg);
}

juce::AudioProcessorEditor* CurrentAudioProcessor::createEditor()
{
    return new CurrentAudioProcessorEditor (*this);
}

// --- Canvas model -----------------------------------------------------------

void CurrentAudioProcessor::refreshEngineConfig()
{
    bool arp = false, rnd = false, scaleGen = false, lfo = false;
    bool chord = false, drone = false;
    bool quant = false, scaleMod = false, progression = false;
    bool shift = false, mirror = false, harmonizer = false;
    bool delay = false, strum = false, humanize = false;
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
                    engRandomGate.store (m.settings.gate);
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
                    engScaleGate.store (m.settings.gate);
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
                    engLfoGate.store (m.settings.gate);
                    engLfoCycle.store (m.settings.lfoCycle);
                    engLfoShape.store (m.settings.lfoShape);
                    engLfoDepthOct.store (m.settings.lfoDepthOct);
                    engLfoDepthSteps.store (m.settings.lfoDepthSteps);
                    engLfoPhase.store (m.settings.lfoPhase);
                }
                lfo = true;
                break;
            case ModuleType::Chord:
                if (! chord)
                {
                    engChordRoot.store (m.settings.rootOverride);
                    engChordScale.store (m.settings.scaleOverride);
                    engChordDegree.store (m.settings.chordDegree);
                    engChordType.store (m.settings.chordType);
                    engChordInversion.store (m.settings.chordInversion);
                    engChordLength.store (m.settings.holdLength);
                    engChordRepeat.store (m.settings.holdRepeat);
                }
                chord = true;
                break;
            case ModuleType::Drone:
                if (! drone)
                {
                    engDroneRoot.store (m.settings.rootOverride);
                    engDroneScale.store (m.settings.scaleOverride);
                    engDroneVoicing.store (m.settings.droneVoicing);
                    engDroneOctave.store (m.settings.droneOctave);
                    engDroneLength.store (m.settings.holdLength);
                    engDroneRepeat.store (m.settings.holdRepeat);
                }
                drone = true;
                break;
            case ModuleType::Quantize:
                if (! quant)
                {
                    engQuantRate.store (m.settings.rate);
                    engQuantSwing.store (m.settings.swing);
                }
                quant = true;
                break;
            case ModuleType::ScaleMod:
                if (! scaleMod)
                {
                    engScaleModRoot.store (m.settings.rootOverride);
                    engScaleModScale.store (m.settings.scaleOverride);
                }
                scaleMod = true;
                break;
            case ModuleType::Progression:
                if (! progression)
                {
                    engProgRoot.store (m.settings.rootOverride);
                    engProgScale.store (m.settings.scaleOverride);
                    engProgRate.store (m.settings.progRate);
                    const int count = juce::jlimit (0, ModuleOptions::kMaxProgSteps,
                                                    (int) m.settings.progSteps.size());
                    for (int i = 0; i < count; ++i)
                        engProgSteps[(size_t) i].store (packProgStep (m.settings.progSteps[(size_t) i]));
                    // Count last: a block that sees the new count sees the new
                    // steps too (each field is independently atomic).
                    engProgCount.store (count);
                }
                progression = true;
                break;
            case ModuleType::Shift:
                if (! shift)
                {
                    engShiftAmount.store (m.settings.shiftAmount);
                    engShiftScale.store (m.settings.scaleOverride);
                    engShiftRoot.store (m.settings.rootOverride);
                }
                shift = true;
                break;
            case ModuleType::Mirror:
                if (! mirror)
                {
                    engMirrorCenter.store (m.settings.mirrorCenter);
                    engMirrorLow.store (m.settings.mirrorLow);
                    engMirrorHigh.store (m.settings.mirrorHigh);
                    engMirrorBounds.store (m.settings.mirrorBounds);
                    engMirrorScale.store (m.settings.scaleOverride);
                    engMirrorRoot.store (m.settings.rootOverride);
                }
                mirror = true;
                break;
            case ModuleType::Harmonizer:
                if (! harmonizer)
                {
                    engHarmType.store (m.settings.harmType);
                    engHarmInversion.store (m.settings.harmInversion);
                    engHarmMode.store (m.settings.harmMode);
                    engHarmScale.store (m.settings.scaleOverride);
                    engHarmRoot.store (m.settings.rootOverride);
                }
                harmonizer = true;
                break;
            case ModuleType::Delay:
                if (! delay)
                {
                    engDelayRate.store (m.settings.rate);
                    engDelayFeedback.store (m.settings.delayFeedback);
                    engDelayShift.store (m.settings.delayShift);
                    engDelayScale.store (m.settings.scaleOverride);
                    engDelayRoot.store (m.settings.rootOverride);
                }
                delay = true;
                break;
            case ModuleType::Strum:
                if (! strum)
                {
                    engStrumSpread.store (m.settings.strumSpread);
                    engStrumMode.store (m.settings.mode);
                    engStrumCurve.store (m.settings.strumCurve);
                    engStrumVelTilt.store (m.settings.strumVelTilt);
                    engStrumJitter.store (m.settings.strumJitter);
                    engStrumRepeat.store (m.settings.repeat);
                }
                strum = true;
                break;
            case ModuleType::Humanize:
                if (! humanize)
                {
                    engHumanizeRate.store (m.settings.rate);
                    engHumanizeSwing.store (m.settings.swing);
                    engHumanizeLayback.store (m.settings.humanizeLayback);
                    engHumanizeAccent.store (m.settings.humanizeAccent);
                    engHumanizeTimeJit.store (m.settings.humanizeTimeJit);
                    engHumanizeVelJit.store (m.settings.humanizeVelJit);
                    engHumanizeLenJit.store (m.settings.humanizeLenJit);
                }
                humanize = true;
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
    engHasChord.store (chord);
    engHasDrone.store (drone);
    engHasQuantize.store (quant);
    engHasScaleMod.store (scaleMod);
    engHasProgression.store (progression);
    engHasShift.store (shift);
    engHasMirror.store (mirror);
    engHasHarmonizer.store (harmonizer);
    engHasDelay.store (delay);
    engHasStrum.store (strum);
    engHasHumanize.store (humanize);
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
    else if (type == ModuleType::Drone)
    {
        // Drones move slower than chords: 4-bar holds back to back (the
        // shared holdLength/holdRepeat default of 1 bar is chord-paced).
        // holdLength indexes barLengthNames, holdRepeat the Repeat list.
        m.settings.holdLength = ModuleOptions::kBarsFourBars;
        m.settings.holdRepeat = ModuleOptions::kRepeatFourBars;
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
            || m.type == ModuleType::Lfo || m.type == ModuleType::ScaleMod
            || m.type == ModuleType::Progression || m.type == ModuleType::Chord
            || m.type == ModuleType::Drone || m.type == ModuleType::Shift
            || m.type == ModuleType::Mirror || m.type == ModuleType::Harmonizer
            || m.type == ModuleType::Delay)
        {
            node.setProperty ("root",  m.settings.rootOverride, nullptr);
            node.setProperty ("scale", m.settings.scaleOverride, nullptr);
        }
        if (m.type == ModuleType::Random || m.type == ModuleType::ScaleGen
            || m.type == ModuleType::Arp || m.type == ModuleType::Lfo
            || m.type == ModuleType::Delay || m.type == ModuleType::Quantize
            || m.type == ModuleType::Humanize)
            node.setProperty ("rate", m.settings.rate, nullptr);
        // Gate ships on every note-emitting Rate module.
        if (m.type == ModuleType::Random || m.type == ModuleType::ScaleGen
            || m.type == ModuleType::Lfo || m.type == ModuleType::Arp)
            node.setProperty ("gate", m.settings.gate, nullptr);
        // Quantize and Humanize share the swing field (same meaning); Humanize
        // adds its five feel amounts.
        if (m.type == ModuleType::Quantize || m.type == ModuleType::Humanize)
            node.setProperty ("swing", m.settings.swing, nullptr);
        if (m.type == ModuleType::Humanize)
        {
            node.setProperty ("humanizeLayback", m.settings.humanizeLayback, nullptr);
            node.setProperty ("humanizeAccent",  m.settings.humanizeAccent, nullptr);
            node.setProperty ("humanizeTimeJit", m.settings.humanizeTimeJit, nullptr);
            node.setProperty ("humanizeVelJit",  m.settings.humanizeVelJit, nullptr);
            node.setProperty ("humanizeLenJit",  m.settings.humanizeLenJit, nullptr);
        }
        if (m.type == ModuleType::Progression)
        {
            node.setProperty ("progRate",  m.settings.progRate, nullptr);
            node.setProperty ("progSteps", progStepsToString (m.settings.progSteps), nullptr);
        }
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
        if (m.type == ModuleType::Shift)
            // root/scale ride the shared block above (scale carries Shift's
            // chromatic/degree switch via kScaleOff).
            node.setProperty ("shiftAmount", m.settings.shiftAmount, nullptr);
        if (m.type == ModuleType::Mirror)
        {
            // root/scale ride the shared block above (Off = chromatic mirror).
            node.setProperty ("mirrorCenter", m.settings.mirrorCenter, nullptr);
            node.setProperty ("mirrorLow",    m.settings.mirrorLow, nullptr);
            node.setProperty ("mirrorHigh",   m.settings.mirrorHigh, nullptr);
            node.setProperty ("mirrorBounds", m.settings.mirrorBounds, nullptr);
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
        if (m.type == ModuleType::Chord)
        {
            node.setProperty ("degree",    m.settings.chordDegree, nullptr);
            node.setProperty ("chordType", m.settings.chordType, nullptr);
            node.setProperty ("inversion", m.settings.chordInversion, nullptr);
        }
        if (m.type == ModuleType::Drone)
        {
            node.setProperty ("voicing",     m.settings.droneVoicing, nullptr);
            node.setProperty ("droneOctave", m.settings.droneOctave, nullptr);
        }
        if (m.type == ModuleType::Chord || m.type == ModuleType::Drone)
        {
            node.setProperty ("holdLength", m.settings.holdLength, nullptr);
            node.setProperty ("holdRepeat", m.settings.holdRepeat, nullptr);
        }
        if (m.type == ModuleType::Harmonizer)
        {
            // root/scale ride the shared block above (Off = chromatic stacking).
            node.setProperty ("harmType",      m.settings.harmType, nullptr);
            node.setProperty ("harmInversion", m.settings.harmInversion, nullptr);
            node.setProperty ("harmMode",      m.settings.harmMode, nullptr);
        }
        if (m.type == ModuleType::Strum)
        {
            // Direction rides the shared "mode", Repeat the shared "repeat"
            // (both read back generically in setStateInformation).
            node.setProperty ("mode",         m.settings.mode, nullptr);
            node.setProperty ("repeat",       m.settings.repeat, nullptr);
            node.setProperty ("strumSpread",  m.settings.strumSpread, nullptr);
            node.setProperty ("strumCurve",   m.settings.strumCurve, nullptr);
            node.setProperty ("strumVelTilt", m.settings.strumVelTilt, nullptr);
            node.setProperty ("strumJitter",  m.settings.strumJitter, nullptr);
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

            // Each type saves only the fields it uses, so the rest fall back
            // to the struct's defaults; types whose drop-time defaults differ
            // from the struct's (see addModule) restate them so an untouched
            // module reloads as it was dropped. Not a backward-compat path —
            // we're pre-release (see CLAUDE.md): old in-development saves
            // just load defaults.
            ModuleSettings def;
            const bool isScaleGen = m.type == ModuleType::ScaleGen;
            const bool isDelay    = m.type == ModuleType::Delay;
            const bool isDrone    = m.type == ModuleType::Drone;
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
            m.settings.mirrorCenter  = (int)  node.getProperty ("mirrorCenter", def.mirrorCenter);
            m.settings.mirrorLow     = (int)  node.getProperty ("mirrorLow",    def.mirrorLow);
            m.settings.mirrorHigh    = (int)  node.getProperty ("mirrorHigh",   def.mirrorHigh);
            m.settings.mirrorBounds  = (int)  node.getProperty ("mirrorBounds", def.mirrorBounds);
            m.settings.swing         = (int)  node.getProperty ("swing", def.swing);
            m.settings.humanizeLayback = (int) node.getProperty ("humanizeLayback", def.humanizeLayback);
            m.settings.humanizeAccent  = (int) node.getProperty ("humanizeAccent",  def.humanizeAccent);
            m.settings.humanizeTimeJit = (int) node.getProperty ("humanizeTimeJit", def.humanizeTimeJit);
            m.settings.humanizeVelJit  = (int) node.getProperty ("humanizeVelJit",  def.humanizeVelJit);
            m.settings.humanizeLenJit  = (int) node.getProperty ("humanizeLenJit",  def.humanizeLenJit);
            m.settings.progRate      = (int)  node.getProperty ("progRate", def.progRate);
            m.settings.progSteps     = progStepsFromString (
                                           node.getProperty ("progSteps",
                                                             progStepsToString (def.progSteps)).toString());
            m.settings.lfoShape      = (int)  node.getProperty ("lfoShape", def.lfoShape);
            m.settings.lfoCycle      = (int)  node.getProperty ("lfoCycle", def.lfoCycle);
            m.settings.lfoDepthOct   = (int)  node.getProperty ("lfoDepthOct", def.lfoDepthOct);
            m.settings.lfoDepthSteps = (int)  node.getProperty ("lfoDepthSteps", def.lfoDepthSteps);
            m.settings.lfoPhase      = (int)  node.getProperty ("lfoPhase", def.lfoPhase);
            m.settings.delayFeedback = (int)  node.getProperty ("feedback", def.delayFeedback);
            m.settings.delayShift    = (int)  node.getProperty ("delayShift", def.delayShift);
            m.settings.chordDegree    = (int) node.getProperty ("degree", def.chordDegree);
            m.settings.chordType      = (int) node.getProperty ("chordType", def.chordType);
            m.settings.chordInversion = (int) node.getProperty ("inversion", def.chordInversion);
            m.settings.droneVoicing   = (int) node.getProperty ("voicing", def.droneVoicing);
            m.settings.droneOctave    = (int) node.getProperty ("droneOctave", def.droneOctave);
            m.settings.holdLength     = (int) node.getProperty ("holdLength",
                                          isDrone ? ModuleOptions::kBarsFourBars : def.holdLength);
            m.settings.holdRepeat     = (int) node.getProperty ("holdRepeat",
                                          isDrone ? ModuleOptions::kRepeatFourBars : def.holdRepeat);
            m.settings.strumSpread    = (int) node.getProperty ("strumSpread", def.strumSpread);
            m.settings.strumCurve     = (int) node.getProperty ("strumCurve", def.strumCurve);
            m.settings.strumVelTilt   = (int) node.getProperty ("strumVelTilt", def.strumVelTilt);
            m.settings.strumJitter    = (int) node.getProperty ("strumJitter", def.strumJitter);
            m.settings.harmType       = (int) node.getProperty ("harmType", def.harmType);
            m.settings.harmInversion  = (int) node.getProperty ("harmInversion", def.harmInversion);
            m.settings.harmMode       = (int) node.getProperty ("harmMode", def.harmMode);

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
