#include "Engine.h"
#include "ScaleTables.h"
#include "ModuleSettings.h"   // ModuleOptions::kMode* — the shared mode indices
#include <algorithm>
#include <cmath>

namespace
{
    // Run `emit(channel)` once per Output-module channel, or once with the
    // event's own channel when no Output module is narrowing things (mask 0).
    template <typename Fn>
    void forEachOutChannel (std::uint16_t outMask, int ownChannel, Fn&& emit)
    {
        if (outMask == 0)
        {
            emit (ownChannel);
            return;
        }
        for (int ch = 1; ch <= 16; ++ch)
            if ((outMask & (1u << (ch - 1))) != 0)
                emit (ch);
    }

    bool inputAccepts (std::uint16_t inMask, int channel)
    {
        return channel >= 1 && channel <= 16
            && (inMask & (1u << (channel - 1))) != 0;
    }

    // Echoes quieter than this aren't scheduled — the velocity decay is what
    // terminates the Delay's feedback chain.
    constexpr int kMinEchoVelocity = 5;
}

void Engine::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void Engine::reset()
{
    held.fill (false);
    activeGen.clear();
    activePass.clear();
    pendingEchoes.clear();
    arpSamplesToNext = 0.0;
    randomSamplesToNext = 0.0;
    scaleSamplesToNext = 0.0;
    lfoSamplesToNext = 0.0;
    arpIndex = 0;
    arpStep = 0;
    scaleStep = 0;
    lfoStep = 0;
    wasPlaying = false;
}

int Engine::mapPitch (int note, int root, int scaleIndex,
                      bool globalQuantize, const Config& cfg) const
{
    int p = note;
    if (cfg.hasQuantize || globalQuantize)
        p = ScaleTables::snapToScale (p, root, scaleIndex);
    // Amount 0 is a strict no-op: degree-shifting snaps out-of-scale notes to
    // the scale as part of the walk, but an idle Shift must not quantize.
    if (cfg.hasShift && cfg.shiftAmount != 0)
    {
        if (cfg.shiftScale == ModuleOptions::kScaleOff)
            p = juce::jlimit (0, 127, p + cfg.shiftAmount);
        else
            p = ScaleTables::stepInScale (p, root,
                                          cfg.shiftScale >= 0 ? cfg.shiftScale : scaleIndex,
                                          cfg.shiftAmount);
    }
    return p;
}

void Engine::flushGeneratedNotes (juce::MidiBuffer& midi, int sample)
{
    for (const auto& a : activeGen)
        midi.addEvent (juce::MidiMessage::noteOff (a.channel, a.note), sample);
    activeGen.clear();
}

void Engine::flushPassedNotes (juce::MidiBuffer& midi, int sample)
{
    for (const auto& p : activePass)
        midi.addEvent (juce::MidiMessage::noteOff (p.outChannel, p.outNote), sample);
    activePass.clear();
}

void Engine::process (juce::MidiBuffer& midi,
                      int numSamples,
                      const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos,
                      int root, int scaleIndex, bool globalQuantize,
                      const Config& cfg)
{
    // No modules on the canvas → true pass-through. If modules were just removed
    // while notes were still sounding, release them (generated and mapped
    // pass-through alike) so nothing hangs — the raw note-offs arriving from now
    // on would no longer match what the chain emitted.
    if (! cfg.anyModule())
    {
        if (! activeGen.empty())
            flushGeneratedNotes (midi, 0);
        if (! activePass.empty())
            flushPassedNotes (midi, 0);
        held.fill (false);
        pendingEchoes.clear();
        wasPlaying = false;
        return;
    }

    bool isPlaying = false;
    double bpm = 120.0;
    if (pos.hasValue())
    {
        isPlaying = pos->getIsPlaying();
        if (auto b = pos->getBpm())
            if (*b > 0.0)
                bpm = *b;
    }

    const double samplesPerQn = juce::jmax (1.0, (60.0 / bpm) * sr);

    // Transport start: align every stepped module's clock so its first step
    // fires at sample 0, and rewind the Arp/Scale patterns to their beginning.
    if (isPlaying && ! wasPlaying)
    {
        arpSamplesToNext = 0.0;
        randomSamplesToNext = 0.0;
        scaleSamplesToNext = 0.0;
        lfoSamplesToNext = 0.0;
        arpIndex = 0;
        arpStep = 0;
        scaleStep = 0;
        lfoStep = 0;
    }

    // Capture the host's incoming events, then rebuild the buffer.
    juce::MidiBuffer incoming;
    incoming.swapWith (midi);

    // Transport stop: everything the engine generated is released and the
    // Delay's buffered echoes are discarded (the shared transport rule).
    // Passed-through host notes are not flushed — the host still owes their
    // note-offs (a live key is released independently of the transport).
    if (! isPlaying && wasPlaying)
    {
        flushGeneratedNotes (midi, 0);
        pendingEchoes.clear();
    }
    wasPlaying = isPlaying;

    // While the arp is running it consumes the host notes (they are its input),
    // so they don't also pass straight through. When stopped, host notes pass
    // through so live playing stays audible.
    const bool swallowHostNotes = cfg.hasArp && isPlaying;

    // Delay: every emitted note-on (pass-through and generated) books its first
    // echo here; a fired echo books the next one the same way. The chain ends
    // when the decayed velocity drops below the floor or the shifted pitch
    // leaves the MIDI range (deliberately not clamped — repeats piling up at
    // the range edge sound worse than the run just ending).
    const double delaySamples = juce::jmax (1.0, samplesPerQn * cfg.delayTimeQn);
    auto scheduleEcho = [&] (int srcNote, int channel, int srcVelocity, int atSample)
    {
        if (! cfg.hasDelay)
            return;
        const int v = juce::roundToInt ((double) srcVelocity * cfg.delayFeedback);
        const int n = srcNote + cfg.delayShift;
        if (v < kMinEchoVelocity || n < 0 || n > 127)
            return;
        pendingEchoes.push_back ({ n, channel, v, atSample + (int) delaySamples });
    };

    // --- Host events: input filter, held tracking, pass-through -------------
    for (const auto meta : incoming)
    {
        const auto m = meta.getMessage();
        const int  s = meta.samplePosition;

        if (m.isNoteOn())
        {
            // The channel filter is the graph's front door: a rejected note-on
            // doesn't reach the arp either.
            if (! inputAccepts (cfg.inChannelMask, m.getChannel()))
                continue;

            held[(size_t) m.getNoteNumber()] = true;
            if (! swallowHostNotes)
            {
                const int p = mapPitch (m.getNoteNumber(), root, scaleIndex,
                                        globalQuantize, cfg);
                forEachOutChannel (cfg.outChannelMask, m.getChannel(), [&] (int ch)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (ch, p, (juce::uint8) m.getVelocity()), s);
                    activePass.push_back ({ m.getNoteNumber(), m.getChannel(), p, ch });
                    scheduleEcho (p, ch, m.getVelocity(), s);
                });
            }
        }
        else if (m.isNoteOff())
        {
            // Note-offs bypass the input filter and any swallowing: whatever was
            // actually emitted for this key (recorded in activePass) is released
            // exactly as it sounded, even if the config changed since the on.
            held[(size_t) m.getNoteNumber()] = false;
            for (auto it = activePass.begin(); it != activePass.end();)
            {
                if (it->inNote == m.getNoteNumber() && it->inChannel == m.getChannel())
                {
                    midi.addEvent (juce::MidiMessage::noteOff (it->outChannel, it->outNote), s);
                    it = activePass.erase (it);
                }
                else
                    ++it;
            }
        }
        else if (m.getChannel() > 0)
        {
            // CC / pitch-bend / aftertouch: same channel filter and Output
            // stamping as notes, so a controller follows its notes to the synth.
            if (! inputAccepts (cfg.inChannelMask, m.getChannel()))
                continue;
            forEachOutChannel (cfg.outChannelMask, m.getChannel(), [&] (int ch)
            {
                auto copy = m;
                copy.setChannel (ch);
                midi.addEvent (copy, s);
            });
        }
        else
        {
            midi.addEvent (m, s);   // non-channel messages (clock, sysex) pass untouched
        }
    }

    // --- Stepped modules: each fires on its own step grid --------------------
    if (isPlaying)
    {
        // Map through the modulator chain and emit on the Output channel(s),
        // remembering the note for its gate-timed release.
        auto emitGenerated = [&] (int rawPitch, int sample, int gateSamples)
        {
            const int p = mapPitch (rawPitch, root, scaleIndex, globalQuantize, cfg);
            forEachOutChannel (cfg.outChannelMask, 1, [&] (int ch)
            {
                midi.addEvent (juce::MidiMessage::noteOn (ch, p, (juce::uint8) 100), sample);
                activeGen.push_back ({ p, ch, sample + gateSamples });
                scheduleEcho (p, ch, 100, sample);
            });
        };

        // Walk one module's step clock across this block, firing `step` at
        // each grid point and carrying the remainder into the next block. The
        // gate is capped one sample short of the step so even a 100% gate's
        // note-off can't collide with the next same-pitch note-on.
        auto runSteps = [&] (double& samplesToNext, double stepQn, double gateFrac, auto&& step)
        {
            const double stepSamples = juce::jmax (1.0, samplesPerQn * stepQn);
            const int    gateSamples = juce::jlimit (1, juce::jmax (1, (int) stepSamples - 1),
                                                     (int) (stepSamples * gateFrac));
            double sPos = samplesToNext;
            while (sPos < (double) numSamples)
            {
                step ((int) sPos, gateSamples);
                sPos += stepSamples;
            }
            samplesToNext = sPos - (double) numSamples;
        };

        // How many steps fit in a repeat window; 0 = Endless (no window).
        auto stepsPerWindow = [] (double repeatQn, double stepQn)
        {
            return repeatQn > 0.0
                ? juce::jmax (1, (int) std::llround (repeatQn / juce::jmax (0.001, stepQn)))
                : 0;
        };

        if (cfg.hasArp)
        {
            // The walk sequence: held notes ascending, repeated per octave of
            // the span (the classic arp octave extension).
            const int octs = juce::jlimit (1, 4, cfg.arpOctaves);
            std::vector<int> seq;
            for (int o = 0; o < octs; ++o)
                for (int n = 0; n < 128; ++n)
                    if (held[(size_t) n])
                        seq.push_back (juce::jmin (127, n + o * 12));

            const int window = stepsPerWindow (cfg.arpRepeatQn, cfg.arpStepQn);

            runSteps (arpSamplesToNext, cfg.arpStepQn, cfg.arpGateFrac, [&] (int s, int gate)
            {
                // Window boundaries are counted on the grid from transport
                // start (arpStep), independent of when notes are held; the walk
                // position itself (arpIndex) only advances when a note fires.
                if (window > 0 && arpStep % window == 0)
                    arpIndex = 0;
                ++arpStep;

                if (seq.empty())
                    return;

                const int n = (int) seq.size();
                int raw = seq.front();
                switch (cfg.arpMode)
                {
                    case ModuleOptions::kModeDown:
                        raw = seq[(size_t) (n - 1 - (arpIndex % n))];
                        break;
                    case ModuleOptions::kModeUpDown:
                    {
                        // Classic up-down: endpoints aren't doubled, so the
                        // cycle is 2n-2 steps (n > 1).
                        const int cycle = n > 1 ? 2 * n - 2 : 1;
                        const int i = arpIndex % cycle;
                        raw = seq[(size_t) (i < n ? i : 2 * (n - 1) - i)];
                        break;
                    }
                    case ModuleOptions::kModeRandom:
                        raw = seq[(size_t) rng.nextInt (n)];
                        break;
                    default:   // kModeUp
                        raw = seq[(size_t) (arpIndex % n)];
                        break;
                }
                ++arpIndex;
                emitGenerated (raw, s, gate);
            });
        }

        if (cfg.hasRandom)
        {
            // All in-scale pitches inside the module's range; drawn uniformly.
            const int rRoot  = cfg.randomRoot  >= 0 ? cfg.randomRoot  : root;
            const int rScale = cfg.randomScale >= 0 ? cfg.randomScale : scaleIndex;
            const int lo = juce::jlimit (0, 127, juce::jmin (cfg.randomFrom, cfg.randomTo));
            const int hi = juce::jlimit (0, 127, juce::jmax (cfg.randomFrom, cfg.randomTo));

            std::vector<int> candidates;
            for (int n = lo; n <= hi; ++n)
                if (ScaleTables::isInScale (n, rRoot, rScale))
                    candidates.push_back (n);
            // A degenerate range can miss the scale entirely (e.g. from == to
            // on a non-scale pitch); snap rather than fall silent.
            if (candidates.empty())
                candidates.push_back (ScaleTables::snapToScale ((lo + hi) / 2, rRoot, rScale));

            runSteps (randomSamplesToNext, cfg.randomStepQn, 0.5, [&] (int s, int gate)
            {
                emitGenerated (candidates[(size_t) rng.nextInt ((int) candidates.size())], s, gate);
            });
        }

        if (cfg.hasScaleGen)
        {
            // Build the pattern: the scale walked from the root at octave 3
            // (MIDI 48 + root) across `scaleOctaves`, optionally capped with
            // the octave root; Down plays the same notes reversed.
            const int sRoot  = cfg.scaleRoot  >= 0 ? cfg.scaleRoot  : root;
            const int sScale = cfg.scaleScale >= 0 ? cfg.scaleScale : scaleIndex;
            const int base   = 48 + juce::jlimit (0, 11, sRoot);
            const int octs   = juce::jlimit (1, 4, cfg.scaleOctaves);

            std::vector<int> pattern;
            for (int o = 0; o < octs; ++o)
                for (int iv : ScaleTables::intervalsForScale (sScale))
                    pattern.push_back (juce::jlimit (0, 127, base + o * 12 + iv));
            if (cfg.scaleEndOnRoot)
                pattern.push_back (juce::jlimit (0, 127, base + octs * 12));
            if (cfg.scaleMode == ModuleOptions::kModeDown)
                std::reverse (pattern.begin(), pattern.end());

            // Endless (window 0) loops the pattern back-to-back: the window is
            // simply the pattern's own length.
            const int window = stepsPerWindow (cfg.scaleRepeatQn, cfg.scaleStepQn);
            const int stepsPerRepeat = window > 0 ? window : (int) pattern.size();

            runSteps (scaleSamplesToNext, cfg.scaleStepQn, 0.5, [&] (int s, int gate)
            {
                // Position inside the repeat window; steps past the pattern's
                // end are rests until the window wraps.
                const int idx = scaleStep % stepsPerRepeat;
                ++scaleStep;
                if (idx < (int) pattern.size())
                    emitGenerated (pattern[(size_t) idx], s, gate);
            });
        }

        if (cfg.hasLfo)
        {
            const int lRoot  = cfg.lfoRoot  >= 0 ? cfg.lfoRoot  : root;
            const int lScale = cfg.lfoScale >= 0 ? cfg.lfoScale : scaleIndex;
            const int centre = 48 + juce::jlimit (0, 11, lRoot);   // root at octave 3,
                                                                   // like the Scale gen
            // Depth in scale degrees: whole octaves are one scale-length each
            // (7 degrees in a 7-note scale, 12 in Chromatic) plus extra steps.
            const int degreesPerOctave = (int) ScaleTables::intervalsForScale (lScale).size();
            const int depthDegrees = juce::jmax (0, cfg.lfoDepthOct) * degreesPerOctave
                                   + juce::jmax (0, cfg.lfoDepthSteps);
            const double cycleQn = juce::jmax (0.001, cfg.lfoCycleQn);

            runSteps (lfoSamplesToNext, cfg.lfoStepQn, 0.5, [&] (int s, int gate)
            {
                // Position inside the cycle, from the grid position since
                // transport start plus the start-phase offset.
                const double x = std::fmod ((double) lfoStep * cfg.lfoStepQn / cycleQn
                                                + cfg.lfoPhase, 1.0);
                ++lfoStep;

                double v = 0.0;   // bipolar shape value at x
                switch (cfg.lfoShape)
                {
                    case ModuleOptions::kLfoTriangle:
                        // 0 rising at phase 0, +1 at 90°, -1 at 270° — aligned
                        // with the sine so swapping shapes keeps the phase feel.
                        v = x < 0.25 ? 4.0 * x
                          : x < 0.75 ? 2.0 - 4.0 * x
                                     : 4.0 * x - 4.0;
                        break;
                    case ModuleOptions::kLfoSawUp:    v = 2.0 * x - 1.0; break;
                    case ModuleOptions::kLfoSawDown:  v = 1.0 - 2.0 * x; break;
                    case ModuleOptions::kLfoSquare:   v = x < 0.5 ? 1.0 : -1.0; break;
                    case ModuleOptions::kLfoRandom:
                        v = rng.nextDouble() * 2.0 - 1.0;   // fresh draw per note
                        break;
                    default:   // kLfoSine
                        v = std::sin (x * juce::MathConstants<double>::twoPi);
                        break;
                }

                const int offset = (int) std::llround (v * (double) depthDegrees);
                emitGenerated (ScaleTables::stepInScale (centre, lRoot, lScale, offset),
                               s, gate);
            });
        }
    }

    // --- Delay: fire the echoes due this block --------------------------------
    // Runs whether or not the transport is playing (a live key echoes too).
    // Index loop on purpose: a fired echo appends its successor, which may
    // itself be due within this block (short delay times / big buffers) and is
    // then reached later in the same sweep.
    if (! cfg.hasDelay)
    {
        // Module removed mid-chain: its buffered echoes go with it (already-
        // sounding ones release normally through activeGen).
        pendingEchoes.clear();
    }
    else if (! pendingEchoes.empty())
    {
        const int gateSamples = juce::jmax (1, (int) (delaySamples * 0.5) - 1);
        for (size_t i = 0; i < pendingEchoes.size(); ++i)
        {
            const auto ec = pendingEchoes[i];   // by value — push_back may reallocate
            if (ec.samplesUntil >= numSamples)
                continue;
            midi.addEvent (juce::MidiMessage::noteOn (ec.channel, ec.note,
                                                      (juce::uint8) ec.velocity),
                           juce::jmax (0, ec.samplesUntil));
            activeGen.push_back ({ ec.note, ec.channel, ec.samplesUntil + gateSamples });
            scheduleEcho (ec.note, ec.channel, ec.velocity, ec.samplesUntil);
            pendingEchoes[i].velocity = 0;   // mark as fired for the sweep below
        }
        pendingEchoes.erase (std::remove_if (pendingEchoes.begin(), pendingEchoes.end(),
                                             [] (const EchoNote& e) { return e.velocity == 0; }),
                             pendingEchoes.end());
        for (auto& e : pendingEchoes)
            e.samplesUntil -= numSamples;
    }

    // --- Release generated notes whose gate ends this block ------------------
    for (auto it = activeGen.begin(); it != activeGen.end();)
    {
        if (it->samplesLeft < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (it->channel, it->note),
                           juce::jmax (0, it->samplesLeft));
            it = activeGen.erase (it);
        }
        else
        {
            it->samplesLeft -= numSamples;
            ++it;
        }
    }
}
