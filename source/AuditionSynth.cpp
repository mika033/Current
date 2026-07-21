#include "AuditionSynth.h"

// Voice implementation ported from Little Arp Monster. Classic plucky
// analog-style voice, tuned for previewing generated lines:
//   * Two slightly-detuned naive sawtooth stacks (supersaw) for the
//     thickness/chorus of an analog synth pluck. Naive (non-bandlimited)
//     is intentional — aliasing on top notes adds character at the cost
//     of nothing a monitoring synth cares about.
//   * Sub-octave sine for body so chords don't sound thin in the low end.
//   * Per-voice resonant low-pass (TPT SVF) with its OWN fast envelope —
//     this is the "filter smack": the filter slams toward base while the
//     amp is still ringing, so each note opens bright and mellows.
//
// Hand-rolled SVF instead of juce::dsp::StateVariableTPTFilter because
// juce_dsp isn't linked — pulling it in for a monitoring synth would bloat
// plugin builds for no reason. Vadim Zavalishin's TPT SVF is ~15 lines.
namespace
{

// Topology-preserving state-variable filter (Zavalishin). Stable across
// the audio band, no warping pre-correction needed for our cutoff range.
struct TPTLowpass
{
    double g = 0.0, R = 1.0; // R = 1/(2Q); smaller R = more resonance
    double s1 = 0.0, s2 = 0.0;
    double sampleRate = 44100.0;

    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() { s1 = 0.0; s2 = 0.0; }

    // cutoffHz clamped to (20, 0.49*sr) — past nyquist tan() blows up.
    void set (double cutoffHz, double Q)
    {
        const double safeCut = juce::jlimit (20.0, sampleRate * 0.49, cutoffHz);
        g = std::tan (juce::MathConstants<double>::pi * safeCut / sampleRate);
        R = 1.0 / (2.0 * juce::jmax (0.5, Q));
    }

    float processLowpass (float xin)
    {
        const double x  = (double) xin;
        const double hp = (x - (2.0 * R + g) * s1 - s2) / (1.0 + 2.0 * R * g + g * g);
        const double bp = g * hp + s1;
        const double lp = g * bp + s2;
        s1 = g * hp + bp;
        s2 = g * bp + lp;
        return (float) lp;
    }
};

class AuditionSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote    (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

// Lock-free per-block view of the Settings-view synth dials. AuditionSynth
// hands every voice the same set of atomic-float pointers (dials owned by
// APVTS, modulation atomics owned by AuditionSynth) at construction; the voice
// reads them once per renderNextBlock so a dial drag updates tone at block
// cadence (~5-10 ms — well under perceived latency). Null-tolerant: a
// default-constructed view yields hard-coded values, so the voice still works
// if param wiring is ever skipped.
struct SynthParamView
{
    std::atomic<float>* cutoffHz  = nullptr;  // 80..18000
    std::atomic<float>* character = nullptr;  // 0..1 — drives detune/spread/Q
    std::atomic<float>* envAmount = nullptr;  // 0..1
    std::atomic<float>* ampDecay  = nullptr;  // seconds

    // CC modulation atomics, 0..1 with 0.5 = CC 64 = neutral. See the member
    // comment in AuditionSynth.h for the CC map and shapes.
    std::atomic<float>* cutoffMod    = nullptr;
    std::atomic<float>* decayMod     = nullptr;
    std::atomic<float>* characterMod = nullptr;
    std::atomic<float>* envAmountMod = nullptr;

    // 1.0 -> the voice sustains a held note instead of plucking it. Pinned on
    // in Current (see the sustainHeld member comment in AuditionSynth.h).
    std::atomic<float>* sustainHeld  = nullptr;
};

class AuditionVoice : public juce::SynthesiserVoice
{
public:
    explicit AuditionVoice (SynthParamView v) : params (v) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<AuditionSound*> (s) != nullptr;
    }

    void setCurrentPlaybackSampleRate (double sr) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate (sr);
        if (sr <= 0.0) return;

        ampAdsr.setSampleRate (sr);
        filterAdsr.setSampleRate (sr);

        // Amp envelope seed; startNote rewrites decay per-note from the Decay
        // dial and picks the sustain/release shape (see sustainHeld).
        juce::ADSR::Parameters amp;
        amp.attack  = 0.003f;
        amp.decay   = 0.25f;
        amp.sustain = 0.0f;
        amp.release = 0.08f;
        ampAdsr.setParameters (amp);

        // Filter envelope: faster than the amp envelope. The mismatch is
        // what gives the "smack" — filter slams toward base while the amp
        // tail is still ringing, so you hear a bright transient -> mellower
        // body. Sustain is held partly open (not 0) so the body keeps some
        // harmonics instead of choking to a dull sine-ish tail.
        juce::ADSR::Parameters fil;
        fil.attack  = 0.002f;
        fil.decay   = 0.12f;
        fil.sustain = 0.25f;
        fil.release = 0.05f;
        filterAdsr.setParameters (fil);

        // Stereo filter pair — one per output channel, so each channel gets
        // its own resonance peak / state (required by the stereo-spread mode
        // at high Character). TPT SVF is ~10 ops/sample, so 32 instances at
        // 16 voices is trivial.
        filterL.prepare (sr);
        filterR.prepare (sr);
    }

    void startNote (int midiNote, float velocity,
                    juce::SynthesiserSound*, int /*currentPitchWheel*/) override
    {
        const double sr = getSampleRate();
        if (sr <= 0.0) return;

        // Detune is character-driven: ±5 ¢ at character=0, ±14 ¢ at
        // character=1. Read once per noteOn (changing detune mid-note would
        // produce audible pitch glides on the in-progress saw deltas); live
        // dial drags pick up on the next note, well within musical perception.
        const float c = readCharacter();
        const double detuneCentsNow = 5.0 + 9.0 * (double) c;
        // Tuned one octave below the incoming MIDI note. A quick-preview
        // voice (not a production instrument): -12 semis gives generated
        // lines fuller body in monitoring without affecting the MIDI we emit.
        const double freq = 440.0 * std::pow (2.0, ((midiNote - 12) - 69) / 12.0);
        const double detune = std::pow (2.0, detuneCentsNow / 1200.0);
        const double centerDeltaA = (freq / detune) / sr;
        const double centerDeltaB = (freq * detune) / sr;
        sub.delta = (freq * 0.5) / sr;

        // Supersaw spread. Each saw is a 3-voice detuned stack; how far the
        // side voices sit from center rides the Character dial, so low
        // Character ≈ a tight pair and high Character ≈ a wide, fat supersaw.
        // The small detune floor keeps the stack shimmering instead of static
        // even at Character 0.
        const double spreadCents = kSpreadFloorCents + (double) c * kSpreadRangeCents;
        for (int v = 0; v < kSawVoices; ++v)
        {
            const double dRatio = std::pow (2.0, (kStackDetune[v] * spreadCents) / 1200.0);
            sawA[v].delta = centerDeltaA * dRatio;
            sawB[v].delta = centerDeltaB * dRatio;
            // Random start phase per voice keeps the stack decorrelated (steady
            // level, no constructive transient spike) and re-rolls the attack
            // shape per note. The half-cycle bias on stack B preserves the
            // anti-cancellation offset between the two stacks.
            sawA[v].phase = rng.nextFloat();
            sawB[v].phase = std::fmod (rng.nextFloat() + 0.5, 1.0);
        }
        sub.phase = rng.nextFloat();

        // Seed the filter coords so the first sample after noteOn is sensible;
        // renderNextBlock re-reads the live dials within ~5-10 ms.
        updateBlockParams (sr);
        filterL.reset();
        filterR.reset();

        // Amp Decay is read at noteOn (per-note cadence): changing decay
        // mid-note doesn't follow gracefully on a juce::ADSR (the running
        // envelope keeps its previous shape).
        //
        // CC 75 (Decay Time) modulates the dial in log-time space: CC 64 =
        // neutral, CC 0 = ÷4 (quick pluck), CC 127 = ×4 (slow tail).
        if (params.ampDecay != nullptr)
        {
            const float modNorm = (params.decayMod != nullptr)
                ? juce::jlimit (0.0f, 1.0f, params.decayMod->load())
                : 0.5f;
            const float modMul = std::pow (2.0f, (modNorm - 0.5f) * 4.0f);
            // Sustaining shape (decay falls to a held level, not silence, and
            // a longer release crossfades chord changes) vs. the zero-sustain
            // pluck. Pinned to sustaining in Current — see AuditionSynth.h.
            const bool holdSustain = (params.sustainHeld != nullptr)
                                     && params.sustainHeld->load() > 0.5f;
            juce::ADSR::Parameters amp;
            amp.attack  = 0.003f;
            amp.decay   = juce::jlimit (0.01f, 4.0f,
                                        params.ampDecay->load() * modMul);
            amp.sustain = holdSustain ? 0.7f : 0.0f;
            amp.release = holdSustain ? 0.20f : 0.08f;
            ampAdsr.setParameters (amp);
        }

        // Per-voice gain kept low enough that 6-note chord stacks don't clip
        // the master bus (saw peaks higher than sine).
        level = velocity * 0.15f;
        ampAdsr.noteOn();
        filterAdsr.noteOn();
        active = true;
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampAdsr.noteOff();
            filterAdsr.noteOff();
        }
        else
        {
            ampAdsr.reset();
            filterAdsr.reset();
            clearCurrentNote();
            active = false;
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override
    {
        if (! active) return;
        // Refresh filter base/peak/Q + character-driven mix weights from
        // APVTS at block cadence so dial drags update tone without
        // per-sample atomic loads.
        const double sr = getSampleRate();
        updateBlockParams (sr);
        const double Q = currentQ;
        const float  wAleft  = sawWeightSelf;  // saw A -> left channel
        const float  wBleft  = sawWeightCross; // saw B -> left channel
        const float  wAright = sawWeightCross;
        const float  wBright = sawWeightSelf;
        constexpr float subMix = kSubLevel;
        const int    numCh   = outputBuffer.getNumChannels();
        constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;
        while (--numSamples >= 0)
        {
            const float ampEnv = ampAdsr.getNextSample();
            const float filEnv = filterAdsr.getNextSample();

            // Linear-Hz cutoff sweep — log-Hz would be ear-linear but the
            // difference is small over a ~120 ms decay and not worth a
            // std::exp() per sample.
            const double cutoff = cutoffBase + (cutoffPeak - cutoffBase) * (double) filEnv;
            filterL.set (cutoff, Q);
            filterR.set (cutoff, Q);

            // Supersaw: sum each saw's 3-voice stack (naive saw, phase ∈ [0,1)
            // -> [-1,1)), advancing each voice's phase as we read it. kStackGain
            // (1/√3) holds the summed level ≈ a single saw regardless of spread.
            float sA = 0.0f, sB = 0.0f;
            for (int v = 0; v < kSawVoices; ++v)
            {
                sA += (float) (2.0 * sawA[v].phase - 1.0);
                sB += (float) (2.0 * sawB[v].phase - 1.0);
                sawA[v].phase += sawA[v].delta; if (sawA[v].phase >= 1.0) sawA[v].phase -= 1.0;
                sawB[v].phase += sawB[v].delta; if (sawB[v].phase >= 1.0) sawB[v].phase -= 1.0;
            }
            sA *= kStackGain;
            sB *= kStackGain;
            const float sS = (float) std::sin (twoPi * sub.phase);

            // Stereo pre-filter mix. At character=0 self=cross=0.45 (both
            // channels identical -> mono); at character=1 self=0.90, cross=0
            // (left = saw A, right = saw B -> hard split). Sub stays centered
            // since panning a sine sub just thins the bass.
            const float mixL = wAleft  * sA + wBleft  * sB + subMix * sS;
            const float mixR = wAright * sA + wBright * sB + subMix * sS;

            const float gainNow = level * ampEnv;
            const float sampleL = filterL.processLowpass (mixL) * gainNow;
            const float sampleR = filterR.processLowpass (mixR) * gainNow;

            // Mono host: sum L+R back down so single-channel listeners hear
            // the same energy. Multi-ch host: write L into 0, R into 1, and
            // duplicate the L sample for any extra channels (rare path).
            if (numCh >= 2)
            {
                outputBuffer.addSample (0, startSample, sampleL);
                outputBuffer.addSample (1, startSample, sampleR);
                for (int ch = numCh; --ch >= 2;)
                    outputBuffer.addSample (ch, startSample, sampleL);
            }
            else if (numCh == 1)
            {
                outputBuffer.addSample (0, startSample, 0.5f * (sampleL + sampleR));
            }

            sub.phase += sub.delta; if (sub.phase >= 1.0) sub.phase -= 1.0;

            ++startSample;

            // Amp envelope governs voice lifetime — free the slot when it's
            // done, or idle voices starve real noteOns.
            if (! ampAdsr.isActive())
            {
                clearCurrentNote();
                active = false;
                break;
            }
        }
    }

private:
    // Clamped read of the Character dial (+ CC 70 additive shift around
    // CC 64). Centralised so startNote (detune) and updateBlockParams
    // (spread/Q) get the same value semantics.
    float readCharacter() const
    {
        const float c = (params.character != nullptr) ? params.character->load() : 0.7f;
        const float modShift = (params.characterMod != nullptr)
            ? juce::jlimit (0.0f, 1.0f, params.characterMod->load()) - 0.5f
            : 0.0f;
        return juce::jlimit (0.0f, 1.0f, c + modShift);
    }

    // Refresh per-block synth params from APVTS. Character drives filter Q
    // (1.0 -> 6.5) and stereo spread (mono -> hard L/R split) in lockstep;
    // detune is read separately in startNote (per-note cadence). Sub level is
    // deliberately NOT character-driven: it sits at a fixed baseline so the
    // low end is a stable layer — "Character" is about width and ping, not
    // body. Peak = base × 2^(envAmt × 5): envAmt 0 parks the filter at base,
    // envAmt 1 sweeps ~5 octaves up at attack and decays back. Nyquist-clamped
    // so the TPT filter doesn't blow up on high cutoff settings.
    void updateBlockParams (double sr)
    {
        const float cutDefault = 4000.0f;
        const float amtDefault = 1.0f;
        const double cutHz   = (params.cutoffHz  != nullptr) ? params.cutoffHz->load()  : cutDefault;
        const double amtDial = (params.envAmount != nullptr) ? params.envAmount->load() : amtDefault;
        // CC 71 (Resonance) re-routed to Env Amount (no dedicated resonance
        // dial — Q rides on Character). Additive shift around CC 64.
        const float envModShift = (params.envAmountMod != nullptr)
            ? juce::jlimit (0.0f, 1.0f, params.envAmountMod->load()) - 0.5f
            : 0.0f;
        const double amt = juce::jlimit (0.0, 1.0, amtDial + (double) envModShift);
        const float  c   = readCharacter();

        // CC 74 (Brightness) modulates the base cutoff in log space — CC 0 =
        // ÷4 (~2 octaves down), CC 127 = ×4. Multiplying base BEFORE openMul
        // preserves the dial-driven sweep range (peak stays 2^(amt*5) octaves
        // above the modulated base).
        const float modNorm = (params.cutoffMod != nullptr)
            ? juce::jlimit (0.0f, 1.0f, params.cutoffMod->load())
            : 0.5f;
        const double modMul = std::pow (2.0, (modNorm - 0.5f) * 4.0);

        const double nyqLimit = (sr > 0.0) ? sr * 0.45 : 18000.0;
        cutoffBase = juce::jlimit (20.0, nyqLimit, cutHz * modMul);
        const double openMul = std::pow (2.0, juce::jlimit (0.0, 1.0, amt) * 5.0);
        cutoffPeak = juce::jlimit (cutoffBase, nyqLimit, cutoffBase * openMul);

        currentQ       = 1.0 + (double) c * 5.5;
        // At c=0: self=cross=0.45 (mono). At c=1: self=0.90, cross=0
        // (saw A -> left only, saw B -> right only).
        sawWeightSelf  = 0.45f + 0.45f * c;
        sawWeightCross = 0.45f - 0.45f * c;
    }

    struct Osc { double phase = 0.0; double delta = 0.0; };
    // Each saw is a 3-voice detuned stack (supersaw); sub stays a single sine.
    // The stack spread rides Character (see startNote), so the one dial moves
    // from a tight pair to a fat, dense wall.
    static constexpr int    kSawVoices        = 3;
    static constexpr double kStackDetune[3]   = { 0.0, -1.0, 1.0 }; // center, low, high
    static constexpr float  kStackGain        = 0.5773503f;         // 1/√3, level-preserving
    static constexpr double kSpreadFloorCents = 3.0;   // subtle shimmer even at Character 0
    static constexpr double kSpreadRangeCents = 18.0;  // Character 1 -> side voices ±21 ¢
    Osc sawA[kSawVoices], sawB[kSawVoices], sub;

    SynthParamView params;

    juce::ADSR ampAdsr, filterAdsr;
    TPTLowpass filterL, filterR;

    double cutoffBase = 300.0;
    double cutoffPeak = 6000.0;
    double currentQ   = 3.0;
    // Fixed sub-oct sine mix — independent of Character so the low end stays
    // a stable layer regardless of width/ping settings.
    static constexpr float kSubLevel = 0.35f;
    float  sawWeightSelf  = 0.675f;   // seeds match character=0.5 mix
    float  sawWeightCross = 0.225f;
    float  level          = 0.0f;
    bool   active         = false;

    // Per-note random source — randomizes each supersaw voice's start phase
    // at noteOn, decorrelating the stack and re-rolling the attack shape per
    // note so repeated notes aren't bit-identical. Audio-thread only.
    juce::Random rng;
};

} // namespace

void AuditionSynth::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterBool;
    using juce::NormalisableRange;

    // Percent formatter lives at the param level (not slider level) because
    // juce::SliderParameterAttachment overwrites the slider's
    // textFromValueFunction in its ctor; the param's own formatter is the only
    // one both the slider text and the DAW's automation editor honour.
    auto pctFromValue = [] (float v, int)
    {
        return juce::String ((int) std::round (v * 100.0f)) + "%";
    };

    // Cutoff range 80 Hz..18 kHz, skew 0.3 for perceptually linear feel.
    // Default 920 Hz (≈ 11 o'clock on the rotary).
    layout.add (std::make_unique<AudioParameterFloat> (cutoffId, "Synth Cutoff",
        NormalisableRange<float> (80.0f, 18000.0f, 1.0f, 0.3f), 920.0f));
    // Character 0..1. Sweeps thickness-related values together. Default 0.3 —
    // boots thin/dry/mono, the user dials up to thicken.
    layout.add (std::make_unique<AudioParameterFloat> (characterId, "Synth Character",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctFromValue)));
    layout.add (std::make_unique<AudioParameterFloat> (envAmtId, "Synth Env Amt",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctFromValue)));
    // Amp decay 0.05..2.0 s, skewed so short values get more travel.
    // Default 0.15 s — snappy pluck.
    layout.add (std::make_unique<AudioParameterFloat> (decayId, "Synth Decay",
        NormalisableRange<float> (0.05f, 2.0f, 0.001f, 0.4f), 0.15f));
    // Combined Space amount — drives delay mix and reverb mix together.
    // Default 0.3 gives a tail without veiling the source.
    layout.add (std::make_unique<AudioParameterFloat> (spaceId, "Synth Space",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctFromValue)));
    // Layout default is OFF — the host/DAW boot state. The Standalone seeds it
    // ON at runtime (the layout can't distinguish wrapper types).
    layout.add (std::make_unique<AudioParameterBool> (enabledId, "Audition Synth", false));
}

void AuditionSynth::attach (juce::AudioProcessorValueTreeState& state, bool createVoices)
{
    enabledParam = state.getRawParameterValue (enabledId);
    spaceParam   = state.getRawParameterValue (spaceId);

    if (! createVoices)
        return;

    // 16 voices is plenty (a 6-note chord at 1/16 with overlap rarely exceeds
    // ~8 active voices once the envelope decays). The shared SynthesiserSound
    // answers true for any note / channel, so output on any MIDI channel is
    // voiced alike.
    SynthParamView view;
    view.cutoffHz     = state.getRawParameterValue (cutoffId);
    view.character    = state.getRawParameterValue (characterId);
    view.envAmount    = state.getRawParameterValue (envAmtId);
    view.ampDecay     = state.getRawParameterValue (decayId);
    view.cutoffMod    = &cutoffMod;
    view.decayMod     = &decayMod;
    view.characterMod = &characterMod;
    view.envAmountMod = &envAmtMod;
    view.sustainHeld  = &sustainHeld;
    for (int i = 0; i < 16; ++i)
        synth.addVoice (new AuditionVoice (view));
    synth.addSound (new AuditionSound());
    voicesCreated = true;
}

void AuditionSynth::prepare (double sr, int maxBlockSize)
{
    if (! voicesCreated)
        return;
    sampleRate = juce::jmax (1.0, sr);

    // Re-pin the sample rate (the host can swap it) and drop any voices that
    // were ringing — a host re-init means sounding state is a lie.
    synth.setCurrentPlaybackSampleRate (sampleRate);
    synth.allNotesOff (0, false);

    // Fixed "medium room" reverb — one mix dial means the user can't dial in
    // a giant cathedral OR a tight ambience, so a single useful spot is picked.
    {
        juce::Reverb::Parameters rp;
        rp.roomSize   = 0.65f;
        rp.damping    = 0.40f;
        rp.wetLevel   = 1.0f;   // hand-mixed in process(); pure wet out
        rp.dryLevel   = 0.0f;
        rp.width      = 1.0f;
        rp.freezeMode = 0.0f;
        reverb.setSampleRate (sampleRate);
        reverb.setParameters (rp);
        reverb.reset();
    }

    // Delay time is recomputed per block from the BPM (one 1/8 note); the
    // buffer is sized for the longest plausible 1/8 — 1 s covers any tempo
    // down to 30 BPM, well past the musically useful range.
    const int delayBufferSize = (int) std::ceil (1.0 * sampleRate) + 4;
    delayLineL.assign ((size_t) delayBufferSize, 0.0f);
    delayLineR.assign ((size_t) delayBufferSize, 0.0f);
    delayWriteIndex = 0;

    // FX-send HPF coefficient. Standard one-pole RC topology:
    //   alpha = 1 / (1 + 2*pi*fc/sr);  y[n] = alpha * (y[n-1] + x[n] - x[n-1])
    // 200 Hz cutoff: gentle 6 dB/oct slope that tames the sub osc and reverb
    // buildup without thinning the body.
    constexpr float kFxHpfCutoffHz = 200.0f;
    fxHpfAlpha = 1.0f / (1.0f + (2.0f * juce::MathConstants<float>::pi
                                 * kFxHpfCutoffHz / (float) sampleRate));
    fxHpfPrevInL = fxHpfPrevOutL = 0.0f;
    fxHpfPrevInR = fxHpfPrevOutR = 0.0f;

    // Start the toggle declick ramp from silence: an enabled synth ramps up
    // over the first blocks rather than punching in at full level.
    fadeGain = 0.0f;

    fxSendBuf.setSize (2, juce::jmax (1, maxBlockSize), false, true, true);
}

void AuditionSynth::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi,
                             int numSamples, double bpm)
{
    if (! voicesCreated)
        return;

    // The toggle is a hard on/off, but we keep rendering while a fade is in
    // flight (fadeGain > 0) so a turn-off tail fades to silence instead of
    // being chopped; the ramp at the bottom declicks both edges.
    const bool synthOn = (enabledParam != nullptr) && enabledParam->load() > 0.5f;
    if (! (synthOn || fadeGain > 0.0001f))
        return;

    // Pre-scan the modulation CCs into their atomics. "Last CC value wins per
    // block" — fine because a CC and its companion noteOn fire together and
    // the pre-scan captures the CC before renderNextBlock sees the noteOn.
    // Channel-agnostic so external controllers drive the synth on any channel.
    for (const auto meta : midi)
    {
        const auto& m = meta.getMessage();
        if (! m.isController()) continue;
        const float norm = m.getControllerValue() / 127.0f;
        switch (m.getControllerNumber())
        {
            case 70: characterMod.store (norm, std::memory_order_release); break;
            case 71: envAmtMod.store (norm, std::memory_order_release);    break;
            case 74: cutoffMod.store (norm, std::memory_order_release);    break;
            case 75: decayMod.store (norm, std::memory_order_release);     break;
            default: break;
        }
    }
    synth.renderNextBlock (buffer, midi, 0, numSamples);

    // Insert FX chain: delay first (so its repeats feed the reverb), then
    // reverb. Mix dials read once per block — plenty for a monitoring synth.
    // At dial = 0 each effect early-outs so the bypass case is bit-exact.
    const int numCh = buffer.getNumChannels();
    // Single Space dial drives both effects with independent curves: the delay
    // caps at 0.8 (a full-dial 1.0 washed out) and the reverb ramps in slowly
    // (the HPF'd send reads wetter than its level suggests).
    const float spaceDial = (spaceParam != nullptr) ? spaceParam->load() : 0.0f;
    const float delayMix  = spaceDial * 0.8f;
    const float reverbWet = spaceDial * 0.4f;

    // Tempo-sync the delay to one 1/8 note, recomputed every block so tempo
    // automation tracks (one block lag is inaudible).
    const double safeBpm = juce::jmax (20.0, bpm);
    const int delayLengthSamples = (int) std::round ((60.0 / safeBpm) * 0.5 * sampleRate);

    if ((delayMix > 0.0001f || reverbWet > 0.0001f) && numCh >= 1)
    {
        // Build the FX-send: HPF'd copy of the dry. Only the path feeding
        // delay and reverb is rolled off — dry stays full-range so the sub osc
        // is still audible on its own, just not piling up in the wet tail.
        auto* dryL  = buffer.getWritePointer (0);
        auto* dryR  = (numCh >= 2) ? buffer.getWritePointer (1) : dryL;
        auto* sendL = fxSendBuf.getWritePointer (0);
        auto* sendR = fxSendBuf.getWritePointer (1);

        const float a = fxHpfAlpha;
        float pInL = fxHpfPrevInL, pOutL = fxHpfPrevOutL;
        float pInR = fxHpfPrevInR, pOutR = fxHpfPrevOutR;
        for (int n = 0; n < numSamples; ++n)
        {
            const float xL = dryL[n];
            const float xR = dryR[n];
            const float yL = a * (pOutL + xL - pInL);
            const float yR = a * (pOutR + xR - pInR);
            sendL[n] = yL; sendR[n] = yR;
            pInL = xL; pOutL = yL;
            pInR = xR; pOutR = yR;
        }
        fxHpfPrevInL = pInL; fxHpfPrevOutL = pOutL;
        fxHpfPrevInR = pInR; fxHpfPrevOutR = pOutR;

        if (delayMix > 0.0001f && delayLengthSamples > 0)
        {
            // Per-channel feedback delay (L stays L, R stays R). Reads the
            // HPF'd send so regenerated repeats are also HPF'd — each round
            // trip doesn't reintroduce sub-frequency energy. Wet mixes into
            // the dry output AND back into the send at the SAME gain, so the
            // reverb hears the delay tail at the level the user hears it.
            const int   bufLen   = (int) delayLineL.size();
            const int   delayLen = juce::jlimit (1, bufLen - 1, delayLengthSamples);
            const float feedback = delayMix * 0.6f;
            const float wetGain  = delayMix * 0.7f;

            for (int n = 0; n < numSamples; ++n)
            {
                const int readIdx = (delayWriteIndex - delayLen + bufLen) % bufLen;
                const float dL = delayLineL[(size_t) readIdx];
                const float dR = delayLineR[(size_t) readIdx];

                delayLineL[(size_t) delayWriteIndex] = sendL[n] + dL * feedback;
                delayLineR[(size_t) delayWriteIndex] = sendR[n] + dR * feedback;

                dryL[n] += dL * wetGain;
                if (numCh >= 2) dryR[n] += dR * wetGain;
                sendL[n] += dL * wetGain;
                sendR[n] += dR * wetGain;

                delayWriteIndex = (delayWriteIndex + 1) % bufLen;
            }
        }

        if (reverbWet > 0.0001f)
        {
            // Reverb processes the (HPF'd send + delay repeats) in place, then
            // mixes into the dry at reverbWet.
            reverb.processStereo (sendL, sendR, numSamples);

            for (int n = 0; n < numSamples; ++n)
            {
                dryL[n] += sendL[n] * reverbWet;
                if (numCh >= 2) dryR[n] += sendR[n] * reverbWet;
            }
        }
    }

    // Declick the toggle. The buffer is otherwise silent, so the synth + FX it
    // now holds IS the whole signal; shaping the buffer fades only the synth.
    // fadeGain is the linear 0..1 fade PHASE, advanced toward the target over
    // ~50 ms and mapped through a raised cosine — a linear gain ramp kinks at
    // the ends of the fade and the ear reads that slope break as a faint
    // click; the cosine has zero slope at both ends. When fully on the loop is
    // skipped and the signal passes bit-exact.
    const float fadeSamples = juce::jmax (1.0f, 0.050f * (float) sampleRate);
    const float target      = synthOn ? 1.0f : 0.0f;
    const float step        = (float) numSamples / fadeSamples;
    const float endPhase    = (target > fadeGain)
                                ? juce::jmin (target, fadeGain + step)
                                : juce::jmax (target, fadeGain - step);
    if ((fadeGain < 1.0f || endPhase < 1.0f) && numCh >= 1)
    {
        const float startPhase = fadeGain;
        const float dPhase     = (endPhase - startPhase) / (float) numSamples;
        auto* chL = buffer.getWritePointer (0);
        auto* chR = (numCh >= 2) ? buffer.getWritePointer (1) : nullptr;
        for (int n = 0; n < numSamples; ++n)
        {
            const float ph = startPhase + dPhase * (float) n;
            const float g  = 0.5f - 0.5f * std::cos (ph * juce::MathConstants<float>::pi);
            chL[n] *= g;
            if (chR != nullptr) chR[n] *= g;
        }
    }
    fadeGain = endPhase;

    // Fully faded out: drop voices + FX state so a later turn-on starts clean.
    // Otherwise the reverb/delay tail would resume mid-ring and the voices
    // could be desynced by note on/offs missed while disabled.
    if (! synthOn && fadeGain <= 0.0001f)
    {
        fadeGain = 0.0f;
        synth.allNotesOff (0, false);
        reverb.reset();
        std::fill (delayLineL.begin(), delayLineL.end(), 0.0f);
        std::fill (delayLineR.begin(), delayLineR.end(), 0.0f);
        delayWriteIndex = 0;
        fxHpfPrevInL = fxHpfPrevOutL = 0.0f;
        fxHpfPrevInR = fxHpfPrevOutR = 0.0f;
    }
}
