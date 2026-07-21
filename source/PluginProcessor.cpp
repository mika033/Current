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

    // Persistence form of Rhythmize's pattern: a 16-char '0'/'1' string,
    // step 0 first — readable in the saved XML like the progression steps.
    juce::String rhythmStepsToString (const std::array<bool, ModuleOptions::kRhythmSteps>& steps)
    {
        juce::String s;
        for (bool on : steps)
            s << (on ? "1" : "0");
        return s;
    }

    std::array<bool, ModuleOptions::kRhythmSteps> rhythmStepsFromString (const juce::String& text)
    {
        auto steps = ModuleOptions::defaultRhythmSteps();
        for (int i = 0; i < ModuleOptions::kRhythmSteps && i < text.length(); ++i)
            steps[(size_t) i] = text[i] != '0';
        return steps;
    }
}

// Bus layout is asymmetric by wrapper:
//
//  * Standalone drops the audio input bus. JUCE's StandaloneFilterWindow paints
//    a persistent "Audio input is muted to avoid feedback loop" notification
//    whenever the processor declares both an input AND an output bus — its
//    feedback-loop heuristic is purely "numIn > 0 && numOut > 0", with no way
//    to opt out. We never read audio, so we just drop the input; the output
//    bus stays so the AudioDeviceManager still spins up a normal audio device.
//
//  * The VST3 plugin keeps a stereo input we never fill: some VST3 hosts (Live)
//    reject an effect-category plugin that exposes no audio input bus, even a
//    MIDI effect. processBlock() clears the buffer.
//
// getPluginLoadedAs() reads the same thread-local JUCE's AudioProcessor base
// ctor uses to set wrapperType, so it is safe to call before that ctor runs.
juce::AudioProcessor::BusesProperties CurrentAudioProcessor::makeBusesProperties()
{
    const auto wrapper = juce::PluginHostType::getPluginLoadedAs();

    // The macOS AU registers as an 'aumi' MIDI processor. auval rejects a MIDI
    // processor that also declares audio buses ("Default Format ... does not
    // match reported Channel handling capabilities"), so the AU gets none.
    // Detecting the wrapper at runtime lets one target serve every format —
    // where Little Arp Monster instead splits the AU into a separate
    // IS_MIDI_EFFECT target to shed the bus at compile time.
    if (wrapper == wrapperType_AudioUnit)
        return BusesProperties();

    if (wrapper == wrapperType_Standalone)
        return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);

    return BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

CurrentAudioProcessor::CurrentAudioProcessor()
    : juce::AudioProcessor (makeBusesProperties()),
      parameters (*this, nullptr, "STATE", createLayout())
{
    rootParam  = parameters.getRawParameterValue (ParamIDs::root);
    scaleParam = parameters.getRawParameterValue (ParamIDs::scale);

    // A fresh DAW instance starts as a wired pass-through — MIDI In → Output —
    // so the plugin makes sound out of the box and the user meets a working
    // cable as an example of the connect gesture. A host restoring a saved
    // project replaces this via setStateInformation. The Standalone instead
    // boots with an empty canvas: it is the dev/test app, state persistence is
    // off there (see getStateInformation), and a clean slate each launch beats
    // meeting last session's leftovers.
    if (! isStandalone())
    {
        const int inId  = addModule (ModuleType::MidiIn, 80.0f, 170.0f);
        const int outId = addModule (ModuleType::Output, 660.0f, 170.0f);
        addConnection (inId, outId);
    }

    // Standalone-only preferences file (the LAM approach). With state
    // persistence off in the Standalone, the two things worth keeping across
    // launches — theme and manual tempo — live here instead: they are user
    // preferences, not patch content.
    if (isStandalone())
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = "Current";
        opts.filenameSuffix      = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.folderName          = "Snorkel Audio";
        opts.storageFormat       = juce::PropertiesFile::storeAsXML;
        standaloneProps = std::make_unique<juce::PropertiesFile> (opts);

        // Default fallback (no settings file yet) matches the APVTS default of
        // index 1 = Dark, so a first-run Standalone lands on Dark.
        const int storedThemeIdx = juce::jlimit (0, 1,
            standaloneProps->getIntValue ("themeIndex", 1));
        if (auto* p = parameters.getParameter (ParamIDs::theme))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) storedThemeIdx));
        parameters.addParameterListener (ParamIDs::theme, this);

        internalBpm.store (juce::jlimit (20.0, 300.0,
            standaloneProps->getDoubleValue ("internalBpm", 120.0)),
            std::memory_order_relaxed);
    }

    // The audition synth can be heard in any wrapper with an audio output bus
    // — everything except the AU MIDI-FX, which sheds its buses (see
    // makeBusesProperties). Where unsupported the synth stays voiceless and
    // processBlock never touches it.
    auditionSynthSupported = (wrapperType != wrapperType_AudioUnit);
    auditionSynth.attach (parameters, auditionSynthSupported);

    // The synth defaults ON in the Standalone (there is no downstream
    // instrument to route to, so booting silent would read as broken) and OFF
    // in a DAW, where the user opts in. Seeded at runtime because the APVTS
    // layout default is compile-time and can't tell wrapper types apart; with
    // Standalone state persistence off this re-seeds on every launch, so the
    // dev app always opens with the monitoring voice live.
    if (isStandalone())
        if (auto* p = parameters.getParameter (AuditionSynth::enabledId))
            p->setValueNotifyingHost (1.0f);
}

CurrentAudioProcessor::~CurrentAudioProcessor()
{
    parameters.removeParameterListener (ParamIDs::theme, this);
}

void CurrentAudioProcessor::parameterChanged (const juce::String&, float newValue)
{
    // Only ever registered for the Theme parameter, Standalone only. Theme
    // edits come from the Settings view on the message thread, so writing the
    // file here is safe.
    if (standaloneProps != nullptr)
    {
        standaloneProps->setValue ("themeIndex", (int) newValue);
        standaloneProps->saveIfNeeded();
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout CurrentAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::root, 1 }, "Root", kRootNames, 0));

    // Default index 1 = Minor (kScaleNames order: Major, Minor, …), so a fresh
    // instance comes up in C Minor rather than C Major.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::scale, 1 }, "Scale", kScaleNames, 1));

    // Default index 1 = Dark, matching CurrentTheme::gActive's default.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::theme, 1 }, "Theme", kThemeNames, 1));

    AuditionSynth::addParameters (layout);

    return layout;
}

void CurrentAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    if (auditionSynthSupported)
        auditionSynth.prepare (sampleRate, samplesPerBlock);
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

    // The engine produces no audio — clear whatever the host handed us. The
    // audition synth (when enabled) writes into the cleared buffer after the
    // engine runs, so it is the only audio the plugin ever emits.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Adopt the newest graph snapshot if the message thread isn't mid-publish;
    // otherwise keep last block's (the swap lands next block).
    {
        juce::SpinLock::ScopedTryLockType tl (graphLock);
        if (tl.isLocked())
            audioGraph = publishedGraph;
    }

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

    // Remember the tempo this block ran at for UI readouts (matches the
    // engine's own bpm resolution: a valid host/internal bpm, else 120).
    {
        double bpm = 120.0;
        if (pos.hasValue())
            if (auto b = pos->getBpm())
                if (*b > 0.0)
                    bpm = *b;
        lastKnownBpm.store (bpm, std::memory_order_relaxed);
    }

    engine.process (midi, buffer.getNumSamples(), pos,
                    root, scaleIndex, audioGraph.get());

    // Voice the outgoing MIDI through the audition synth — after the engine,
    // so the user monitors exactly what the plugin emits (every module,
    // including the timing post-passes, has already run).
    if (auditionSynthSupported)
        auditionSynth.process (buffer, midi, buffer.getNumSamples(),
                               lastKnownBpm.load (std::memory_order_relaxed));
}

juce::AudioProcessorEditor* CurrentAudioProcessor::createEditor()
{
    return new CurrentAudioProcessorEditor (*this);
}

// --- Graph snapshot ----------------------------------------------------------

const ModuleInstance* CurrentAudioProcessor::findModule (int id) const
{
    for (const auto& m : moduleList)
        if (m.id == id)
            return &m;
    return nullptr;
}

// One module's settings resolved to engine units. The option-table conversions
// (rate index -> quarter notes, gate index -> fraction, ...) happen here on the
// message thread, once per model change, so the audio thread only reads plain
// numbers.
ModuleParams CurrentAudioProcessor::paramsFor (const ModuleInstance& m) const
{
    ModuleParams p;
    const auto& s = m.settings;
    p.root  = s.rootOverride;
    p.scale = s.scaleOverride;

    switch (m.type)
    {
        case ModuleType::MidiIn:
        case ModuleType::Output:
            p.channel = m.channel;
            break;
        case ModuleType::Random:
            p.stepQn    = ModuleOptions::rateQuarterNotes (s.rate);
            p.gateFrac  = ModuleOptions::gateFraction (s.gate);
            p.rangeFrom = s.rangeFrom;
            p.rangeTo   = s.rangeTo;
            break;
        case ModuleType::ScaleGen:
            p.stepQn    = ModuleOptions::rateQuarterNotes (s.rate);
            p.gateFrac  = ModuleOptions::gateFraction (s.gate);
            p.repeatQn  = ModuleOptions::repeatQuarterNotes (s.repeat);
            p.octaves   = s.octaves;
            p.mode      = s.mode;
            p.endOnRoot = s.endOnRoot;
            break;
        case ModuleType::Lfo:
            p.stepQn        = ModuleOptions::rateQuarterNotes (s.rate);
            p.gateFrac      = ModuleOptions::gateFraction (s.gate);
            p.lfoCycleQn    = ModuleOptions::barLengthQuarterNotes (s.lfoCycle);
            p.lfoShape      = s.lfoShape;
            p.lfoDepthOct   = s.lfoDepthOct;
            p.lfoDepthSteps = s.lfoDepthSteps;
            p.lfoPhase      = ModuleOptions::lfoPhaseFraction (s.lfoPhase);
            break;
        case ModuleType::Chord:
            p.chordDegree    = s.chordDegree;
            p.chordType      = s.chordType;
            p.chordInversion = s.chordInversion;
            p.holdLengthQn   = ModuleOptions::barLengthQuarterNotes (s.holdLength);
            p.holdPeriodQn   = ModuleOptions::repeatQuarterNotes (s.holdRepeat);
            break;
        case ModuleType::Drone:
            p.droneVoicing = s.droneVoicing;
            p.droneOctave  = s.droneOctave;
            p.holdLengthQn = ModuleOptions::barLengthQuarterNotes (s.holdLength);
            p.holdPeriodQn = ModuleOptions::repeatQuarterNotes (s.holdRepeat);
            break;
        case ModuleType::Arp:
            p.stepQn   = ModuleOptions::rateQuarterNotes (s.rate);
            p.gateFrac = ModuleOptions::gateFraction (s.gate);
            p.repeatQn = ModuleOptions::repeatQuarterNotes (s.repeat);
            p.octaves  = s.octaves;
            p.mode     = s.mode;
            break;
        case ModuleType::Rhythmize:
            p.stepQn   = ModuleOptions::rateQuarterNotes (s.rate);
            p.gateFrac = ModuleOptions::gateFraction (s.gate);
            p.rhythmMask = 0;
            for (int i = 0; i < ModuleOptions::kRhythmSteps; ++i)
                if (s.rhythmSteps[(size_t) i])
                    p.rhythmMask |= (1u << i);
            break;
        case ModuleType::Quantize:
            p.stepQn = ModuleOptions::rateQuarterNotes (s.rate);
            p.swing  = ModuleOptions::swingFraction (s.swing);
            break;
        case ModuleType::ScaleMod:
            break;   // root/scale only
        case ModuleType::Progression:
        {
            p.progRateQn    = ModuleOptions::barLengthQuarterNotes (s.progRate);
            p.progStepCount = juce::jlimit (0, ModuleOptions::kMaxProgSteps,
                                            (int) s.progSteps.size());
            for (int i = 0; i < p.progStepCount; ++i)
            {
                p.progDegrees[(size_t) i] = s.progSteps[(size_t) i].degree;
                p.progOctaves[(size_t) i] = s.progSteps[(size_t) i].octave;
            }
            break;
        }
        case ModuleType::Shift:
            p.shiftAmount = s.shiftAmount;
            break;
        case ModuleType::Mirror:
            p.mirrorCenter = s.mirrorCenter;
            p.mirrorLow    = s.mirrorLow;
            p.mirrorHigh   = s.mirrorHigh;
            p.mirrorBounds = s.mirrorBounds;
            break;
        case ModuleType::Harmonizer:
            p.harmType      = s.harmType;
            p.harmInversion = s.harmInversion;
            p.harmMode      = s.harmMode;
            break;
        case ModuleType::Delay:
            p.stepQn        = ModuleOptions::rateQuarterNotes (s.rate);
            p.delayFeedback = ModuleOptions::feedbackFraction (s.delayFeedback);
            p.delayShift    = s.delayShift;
            break;
        case ModuleType::Strum:
            p.mode           = s.mode;
            p.repeatQn       = ModuleOptions::repeatQuarterNotes (s.repeat);
            p.strumGapQn     = ModuleOptions::strumGapQn (s.strumSpread);
            p.strumCurve     = s.strumCurve;
            p.strumVelTilt   = (double) s.strumVelTilt / (double) ModuleOptions::kStrumVelTiltRange;
            p.strumJitter    = (double) s.strumJitter  / (double) ModuleOptions::kStrumJitterMax;
            break;
        case ModuleType::Humanize:
            p.stepQn          = ModuleOptions::rateQuarterNotes (s.rate);
            p.swing           = ModuleOptions::swingFraction (s.swing);
            p.humanizeLayback = ModuleOptions::swingFraction (s.humanizeLayback);
            p.humanizeAccent  = ModuleOptions::swingFraction (s.humanizeAccent);
            p.humanizeTimeJit = ModuleOptions::swingFraction (s.humanizeTimeJit);
            p.humanizeVelJit  = ModuleOptions::swingFraction (s.humanizeVelJit);
            p.humanizeLenJit  = ModuleOptions::swingFraction (s.humanizeLenJit);
            break;
    }
    return p;
}

void CurrentAudioProcessor::rebuildGraph()
{
    auto snap = std::make_shared<GraphSnapshot>();
    snap->topologyVersion = topologyVersion;

    const int n = (int) moduleList.size();

    // id -> moduleList index.
    auto indexOfId = [this] (int id) -> int
    {
        for (int i = 0; i < (int) moduleList.size(); ++i)
            if (moduleList[(size_t) i].id == id)
                return i;
        return -1;
    };

    // Kahn topological sort over the connections, stable in list order so an
    // unwired canvas keeps its placement order. Cycles can't exist (canConnect
    // refuses them); a defensive leftover-append keeps a corrupted state from
    // dropping modules.
    std::vector<int> indegree ((size_t) n, 0);
    for (const auto& c : connectionList)
    {
        const int ti = indexOfId (c.toId);
        if (indexOfId (c.fromId) >= 0 && ti >= 0)
            ++indegree[(size_t) ti];
    }

    std::vector<int>  order;
    std::vector<bool> placed ((size_t) n, false);
    order.reserve ((size_t) n);
    for (int placedCount = 0; placedCount < n;)
    {
        int pick = -1;
        for (int i = 0; i < n; ++i)
            if (! placed[(size_t) i] && indegree[(size_t) i] == 0)
            {
                pick = i;
                break;
            }
        if (pick < 0)   // cycle in a corrupted state: append the rest as-is
        {
            for (int i = 0; i < n; ++i)
                if (! placed[(size_t) i])
                    order.push_back (i);
            break;
        }
        placed[(size_t) pick] = true;
        order.push_back (pick);
        ++placedCount;
        for (const auto& c : connectionList)
            if (indexOfId (c.fromId) == pick)
            {
                const int ti = indexOfId (c.toId);
                if (ti >= 0)
                    --indegree[(size_t) ti];
            }
    }

    // moduleList index -> position in the snapshot's node vector.
    std::vector<int> nodeIndexOf ((size_t) n, -1);
    for (int i = 0; i < (int) order.size(); ++i)
        nodeIndexOf[(size_t) order[(size_t) i]] = i;

    for (int mi : order)
    {
        const auto& m = moduleList[(size_t) mi];
        GraphNode g;
        g.id     = m.id;
        g.type   = m.type;
        g.params = paramsFor (m);
        for (const auto& c : connectionList)
            if (c.toId == m.id)
            {
                const int fi = indexOfId (c.fromId);
                if (fi >= 0 && nodeIndexOf[(size_t) fi] >= 0)
                    g.inputs.push_back (nodeIndexOf[(size_t) fi]);
            }
        snap->nodes.push_back (std::move (g));
    }

    juce::SpinLock::ScopedLockType l (graphLock);
    publishedGraph = std::move (snap);
}

// --- Canvas model ------------------------------------------------------------

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
        // Default range: root at octave 2 up to root at octave 4 (e.g. C2..C4
        // = MIDI 36..60), taken from the global root at drop time since the
        // module's own root starts on Global. 1/8 draws — the shared 1/16
        // default is too frantic for a random line.
        const int root = (int) (rootParam != nullptr ? rootParam->load() : 0.0f);
        m.settings.rangeFrom = juce::jlimit (0, 127, 36 + root);
        m.settings.rangeTo   = juce::jlimit (0, 127, 60 + root);
        m.settings.rate      = ModuleOptions::kRate1_8;
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
    ++topologyVersion;
    rebuildGraph();
    return m.id;
}

void CurrentAudioProcessor::moveModule (int id, float x, float y)
{
    // Position never reaches the engine, so no graph rebuild.
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
            rebuildGraph();   // settings edit: same topologyVersion, notes ring on
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
            rebuildGraph();   // settings edit: same topologyVersion, notes ring on
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
    // A module takes its cables with it.
    connectionList.erase (std::remove_if (connectionList.begin(), connectionList.end(),
                                          [id] (const ModuleConnection& c)
                                          { return c.fromId == id || c.toId == id; }),
                          connectionList.end());
    ++topologyVersion;
    rebuildGraph();
}

// --- Connections -------------------------------------------------------------

bool CurrentAudioProcessor::canConnect (int fromId, int toId) const
{
    if (fromId == toId)
        return false;
    const auto* from = findModule (fromId);
    const auto* to   = findModule (toId);
    if (from == nullptr || to == nullptr)
        return false;
    if (! moduleHasOutputPort (from->type) || ! moduleHasInputPort (to->type))
        return false;
    for (const auto& c : connectionList)
        if (c.fromId == fromId && c.toId == toId)
            return false;   // duplicate

    // No cycles: refuse if `fromId` is already reachable downstream of `toId`
    // (adding the cable would close the loop). Iterative DFS over the ids.
    std::vector<int> stack { toId }, visited;
    while (! stack.empty())
    {
        const int at = stack.back();
        stack.pop_back();
        if (at == fromId)
            return false;
        if (std::find (visited.begin(), visited.end(), at) != visited.end())
            continue;
        visited.push_back (at);
        for (const auto& c : connectionList)
            if (c.fromId == at)
                stack.push_back (c.toId);
    }
    return true;
}

bool CurrentAudioProcessor::addConnection (int fromId, int toId)
{
    if (! canConnect (fromId, toId))
        return false;
    connectionList.push_back ({ fromId, toId });
    ++topologyVersion;
    rebuildGraph();
    return true;
}

void CurrentAudioProcessor::removeConnection (int fromId, int toId)
{
    const auto before = connectionList.size();
    connectionList.erase (std::remove_if (connectionList.begin(), connectionList.end(),
                                          [&] (const ModuleConnection& c)
                                          { return c.fromId == fromId && c.toId == toId; }),
                          connectionList.end());
    if (connectionList.size() != before)
    {
        ++topologyVersion;
        rebuildGraph();
    }
}

// --- State ------------------------------------------------------------------

void CurrentAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // The Standalone deliberately reports no state: it is the dev/test app,
    // and every launch boots the empty canvas + defaults (the LAM approach).
    // Theme and manual tempo survive via standaloneProps instead, and the
    // audition synth re-seeds ON in the ctor.
    if (isStandalone())
        return;

    // Root of the saved state is the APVTS tree; the canvas layout rides along
    // as a child node. This is DAW-project persistence, not the user-facing
    // Load/Save of patches (which is a later phase).
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
            || m.type == ModuleType::Humanize || m.type == ModuleType::Rhythmize)
            node.setProperty ("rate", m.settings.rate, nullptr);
        // Gate ships on every note-emitting Rate module.
        if (m.type == ModuleType::Random || m.type == ModuleType::ScaleGen
            || m.type == ModuleType::Lfo || m.type == ModuleType::Arp
            || m.type == ModuleType::Rhythmize)
            node.setProperty ("gate", m.settings.gate, nullptr);
        if (m.type == ModuleType::Rhythmize)
            node.setProperty ("rhythmSteps", rhythmStepsToString (m.settings.rhythmSteps), nullptr);
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

    // Cables are stored by the modules' positions in the saved list (not their
    // ids — ids are session-local and reassigned on load).
    auto savedIndexOf = [this] (int id) -> int
    {
        for (int i = 0; i < (int) moduleList.size(); ++i)
            if (moduleList[(size_t) i].id == id)
                return i;
        return -1;
    };
    for (const auto& c : connectionList)
    {
        const int fi = savedIndexOf (c.fromId);
        const int ti = savedIndexOf (c.toId);
        if (fi < 0 || ti < 0)
            continue;
        juce::ValueTree conn ("Connection");
        conn.setProperty ("from", fi, nullptr);
        conn.setProperty ("to",   ti, nullptr);
        canvas.appendChild (conn, nullptr);
    }
    state.appendChild (canvas, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CurrentAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // See getStateInformation — the Standalone never saves, and ignoring a
    // stray restore keeps old saved blobs from resurrecting last session.
    if (isStandalone())
        return;

    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid())
        return;

    // Open the help-bar suppression window (see isRestoringState) before any
    // of the restore work retriggers UI attachments.
    stateRestoreEndsAt.store (juce::Time::getMillisecondCounter() + 200,
                              std::memory_order_release);

    moduleList.clear();
    connectionList.clear();
    nextModuleId = 1;

    if (auto canvas = state.getChildWithName ("Canvas"); canvas.isValid())
    {
        std::vector<int> loadedIds;   // saved index -> new session-local id

        for (const auto& node : canvas)
        {
            if (! node.hasType ("Module"))
                continue;

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
            m.settings.rhythmSteps   = rhythmStepsFromString (
                                           node.getProperty ("rhythmSteps",
                                                             rhythmStepsToString (def.rhythmSteps)).toString());
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

            moduleList.push_back (m);
            loadedIds.push_back (m.id);
        }

        // Cables, by saved module index. canConnect re-validates each (ports,
        // duplicates, cycles), so a hand-edited or corrupted save can't
        // publish an invalid graph.
        for (const auto& node : canvas)
        {
            if (! node.hasType ("Connection"))
                continue;
            const int fi = (int) node.getProperty ("from", -1);
            const int ti = (int) node.getProperty ("to",   -1);
            if (fi < 0 || ti < 0
                || fi >= (int) loadedIds.size() || ti >= (int) loadedIds.size())
                continue;
            if (canConnect (loadedIds[(size_t) fi], loadedIds[(size_t) ti]))
                connectionList.push_back ({ loadedIds[(size_t) fi], loadedIds[(size_t) ti] });
        }

        state.removeChild (canvas, nullptr);
    }

    ++topologyVersion;   // a wholesale replace is a topology change
    rebuildGraph();
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
