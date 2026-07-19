#include "Engine.h"
#include "ScaleTables.h"
#include "ModuleSettings.h"   // ModuleOptions::kMode* — the shared mode indices
#include <algorithm>
#include <cmath>
#include <limits>

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

    // Mathematical mod for grid indices: a negative position (host pre-roll /
    // count-in) must wrap into the pattern, not mirror around zero as C's %
    // would.
    int wrapIndex (juce::int64 i, int n)
    {
        const juce::int64 m = i % (juce::int64) n;
        return (int) (m < 0 ? m + n : m);
    }

    // Pair-based swing (swing-timing.md): the straight grid boundary at index j
    // is pushed late by swingQn when j is odd; even boundaries — the pair starts
    // — stay put. The one source of the swing model, shared by Quantize (which
    // snaps note-ons to the next of these boundaries) and Humanize (which warps
    // through them continuously, see swingWarpQn).
    double swungBoundaryQn (juce::int64 j, double stepQn, double swingQn)
    {
        return (double) j * stepQn + ((j & 1) != 0 ? swingQn : 0.0);
    }

    // Continuous swing warp: map an arbitrary song position through the
    // piecewise-linear time-warp the pair-based model defines (each straight
    // step [j, j+1] is stretched/shrunk onto its swung span). Monotonic, so
    // event order is preserved, and — because pair starts are fixed and the
    // interior only ever moves late — the result is always >= qn. That is what
    // lets Humanize apply swing as a forward-only nudge without snapping to the
    // grid (a real-time MIDI FX can only delay a note, never advance it).
    double swingWarpQn (double qn, double stepQn, double swingQn)
    {
        if (stepQn <= 0.0)
            return qn;
        const juce::int64 j = (juce::int64) std::floor (qn / stepQn);
        const double a    = swungBoundaryQn (j,     stepQn, swingQn);
        const double b    = swungBoundaryQn (j + 1, stepQn, swingQn);
        const double frac = qn / stepQn - (double) j;
        return a + frac * (b - a);
    }

    // A stable pseudo-random value in [0, 1) from integer inputs, so Humanize's
    // "random" jitter is a deterministic function of song position (grid index)
    // and note — the humanized feel then repeats identically on every host loop
    // pass instead of shimmering. `salt` separates the independent draws (timing
    // vs. velocity vs. length). A finalizer-style integer hash (splitmix64-ish).
    double hash01 (juce::int64 gridIndex, int noteKey, int salt)
    {
        std::uint64_t h = (std::uint64_t) gridIndex * 0x9E3779B97F4A7C15ULL;
        h ^= (std::uint64_t) (noteKey + 1) * 0xC2B2AE3D27D4EB4FULL;
        h ^= (std::uint64_t) (salt + 1)    * 0x165667B19E3779F9ULL;
        h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ULL;
        h ^= h >> 27; h *= 0x94D049BB133111EBULL;
        h ^= h >> 31;
        return (double) (h >> 11) * (1.0 / 9007199254740992.0);   // 53-bit mantissa
    }

    // Humanize maxima, as fractions of its groove step (timing) or velocity
    // units, reached at the 100% control setting. Chosen so full jitter is
    // clearly audible but stays musical.
    constexpr double kHumanizeLaybackFrac = 0.5;   // full lay-back = half a step behind
    constexpr double kHumanizeTimeJitFrac = 0.5;   // full timing jitter = up to half a step late
    constexpr double kHumanizeLenJitFrac  = 0.5;   // full length jitter = up to half a step longer
    constexpr double kHumanizeAccentDepth = 0.4;   // full accent = +/-40% velocity on strong/weak
    constexpr double kHumanizeVelRange    = 48.0;  // full velocity jitter = +/-48
    constexpr int    kSaltTime = 1, kSaltVel = 2, kSaltLen = 3;
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
    pendingQuant.clear();
    pendingHuman.clear();
    humanHeld.clear();
    fallbackQn = 0.0;
    wasPlaying = false;
}

int Engine::mapPitch (int note, int root, int scaleIndex,
                      int progIndex, const Config& cfg) const
{
    int p = note;
    // Scale Off means "don't force onto a scale", so the snap is simply skipped
    // — passing notes stay chromatic.
    if (cfg.hasScaleMod && cfg.scaleModScale != ModuleOptions::kScaleOff)
        p = ScaleTables::snapToScale (p,
                                      cfg.scaleModRoot  >= 0 ? cfg.scaleModRoot  : root,
                                      cfg.scaleModScale >= 0 ? cfg.scaleModScale : scaleIndex);
    if (cfg.hasProgression && cfg.progStepCount > 0)
    {
        const int i = juce::jlimit (0, cfg.progStepCount - 1, progIndex);
        const int degree = cfg.progDegrees[(size_t) i];
        const int octave = cfg.progOctaves[(size_t) i];
        // Degree I / octave 0 is a strict no-op (like an idle Shift): the
        // degree walk would otherwise snap out-of-scale notes to the scale.
        if (degree != 0 || octave != 0)
        {
            if (degree != 0)
            {
                // Scale Off walks degrees chromatically (Chromatic's 12 members).
                const int pScale = cfg.progScale == ModuleOptions::kScaleOff
                                       ? ModuleOptions::kChromaticScale
                                       : cfg.progScale >= 0 ? cfg.progScale : scaleIndex;
                p = ScaleTables::stepInScale (p,
                                              cfg.progRoot >= 0 ? cfg.progRoot : root,
                                              pScale, degree);
            }
            p = juce::jlimit (0, 127, p + 12 * octave);
        }
    }
    // Amount 0 is a strict no-op: degree-shifting snaps out-of-scale notes to
    // the scale as part of the walk, but an idle Shift must not quantize.
    if (cfg.hasShift && cfg.shiftAmount != 0)
    {
        if (cfg.shiftScale == ModuleOptions::kScaleOff)
            p = juce::jlimit (0, 127, p + cfg.shiftAmount);
        else
            p = ScaleTables::stepInScale (p,
                                          cfg.shiftRoot >= 0 ? cfg.shiftRoot : root,
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
                      int root, int scaleIndex,
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
        pendingQuant.clear();
        pendingHuman.clear();
        humanHeld.clear();
        wasPlaying = false;
        return;
    }

    bool isPlaying = false;
    double bpm = 120.0;
    juce::Optional<double> hostPpq;
    if (pos.hasValue())
    {
        isPlaying = pos->getIsPlaying();
        if (auto b = pos->getBpm())
            if (*b > 0.0)
                bpm = *b;
        hostPpq = pos->getPpqPosition();
    }

    const double samplesPerQn = juce::jmax (1.0, (60.0 / bpm) * sr);

    // Transport start: nothing to rewind — every grid below is derived from
    // the song position — but the ppq fallback re-anchors to zero so a host
    // that reports no ppq still counts its grids from the moment play was
    // pressed.
    if (isPlaying && ! wasPlaying && ! hostPpq.hasValue())
        fallbackQn = 0.0;

    // The block's song position in quarter notes, owning the half-open range
    // [blockStartQn, blockEndQn): a grid boundary exactly on the block start
    // fires here, one exactly on the end fires next block — so a host loop
    // wrap landing on a boundary can neither double-fire nor skip it. All
    // grids are re-derived from this each block (the LAM master-clock model);
    // there are no counters to drift across loop wraps or tempo changes.
    const double blockStartQn = hostPpq.hasValue() ? *hostPpq : fallbackQn;
    const double blockEndQn   = blockStartQn + (double) numSamples / samplesPerQn;
    // The fallback tracks the block end even while the host is supplying ppq,
    // so a host that stops reporting it mid-run continues seamlessly from the
    // last known position instead of jumping to a stale anchor.
    if (isPlaying)
        fallbackQn = blockEndQn;

    // Capture the host's incoming events, then rebuild the buffer.
    juce::MidiBuffer incoming;
    incoming.swapWith (midi);

    // Transport stop: everything the engine generated is released and the
    // Delay's buffered echoes and Quantize's deferred notes are discarded (the
    // shared transport rule). A discarded host-held note's eventual note-off
    // finds no activePass entry and is simply ignored — nothing hangs.
    // Passed-through host notes are not flushed — the host still owes their
    // note-offs (a live key is released independently of the transport).
    if (! isPlaying && wasPlaying)
    {
        // Humanize first, before the generated-note flush below, so on/off stay
        // exactly balanced across the stop. A note whose humanized note-on is
        // still buffered never reached the synth, so cancel its pending gate in
        // activeGen (otherwise the flush would emit a note-off the synth never
        // got an on for); a buffered note-off is releasing a note the synth IS
        // sounding, so emit it now (its activeGen entry is already gone). The
        // flush then releases everything still genuinely held.
        for (const auto& hev : pendingHuman)
        {
            if (hev.msg.isNoteOn())
            {
                for (auto it = activeGen.begin(); it != activeGen.end(); ++it)
                    if (it->channel == hev.msg.getChannel()
                        && it->note == hev.msg.getNoteNumber())
                    {
                        activeGen.erase (it);
                        break;
                    }
            }
            else if (hev.msg.isNoteOff())
                midi.addEvent (hev.msg, 0);
        }
        pendingHuman.clear();
        humanHeld.clear();

        flushGeneratedNotes (midi, 0);
        pendingEchoes.clear();
        pendingQuant.clear();
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
        // Per-echo shift mirrors Shift: chromatic semitones with the scale Off,
        // scale degrees with a scale active. A zero shift is a strict no-op so
        // an un-shifted echo is never snapped onto a scale.
        int n = srcNote;
        if (cfg.delayShift != 0)
            n = cfg.delayScale == ModuleOptions::kScaleOff
                    ? srcNote + cfg.delayShift
                    : ScaleTables::stepInScale (srcNote,
                                                cfg.delayRoot >= 0 ? cfg.delayRoot : root,
                                                cfg.delayScale >= 0 ? cfg.delayScale : scaleIndex,
                                                cfg.delayShift);
        if (v < kMinEchoVelocity || n < 0 || n > 127)
            return;
        pendingEchoes.push_back ({ n, channel, v, atSample + (int) delaySamples });
    };

    // Quantize needs the transport grid, so it only re-times while playing;
    // stopped, everything passes straight through (live playing stays live).
    const bool   quantActive  = cfg.hasQuantize && isPlaying;
    const double quantStepQn  = juce::jmax (0.001, cfg.quantStepQn);
    const double quantSwingQn = juce::jlimit (0.0, 1.0, cfg.quantSwing)
                                    * 0.5 * quantStepQn;

    // The next swung grid point at or after block sample `s`, as a
    // block-relative sample (possibly past this block's end). Boundary index
    // j counts from the song's bar 0; swing pushes odd boundaries late by
    // swing/2 of a step (pair-based model — even boundaries, the pair
    // starts, stay put), so the parity — and with it the shuffle — is fixed
    // to the song's bars, not to when play was pressed. The scan starts one
    // boundary back because a swung odd point can still be ahead of `s` when
    // its straight position has already passed.
    auto quantTarget = [&] (int s) -> int
    {
        // Half a sample of tolerance: `s` is itself rounded to a whole
        // sample, so a note sitting exactly on a boundary can read as a hair
        // past it — without the allowance it would be deferred a full step.
        const double atQn = blockStartQn + ((double) s - 0.5) / samplesPerQn;
        for (auto j = (juce::int64) std::floor (atQn / quantStepQn) - 1; ; ++j)
        {
            // (j & 1) is 1 for negative odd values too, so pre-roll
            // boundaries keep the same parity rule. Shared swing helper — the
            // one place the pair-based model lives (Humanize warps through it).
            const double swungQn = swungBoundaryQn (j, quantStepQn, quantSwingQn);
            if (swungQn >= atQn)
                return juce::jmax (s, (int) std::llround ((swungQn - blockStartQn)
                                                              * samplesPerQn));
        }
    };

    // Which progression step applies at block sample `s` (which may lie past
    // this block for quantize-deferred notes — the pitch is decided by the
    // step in force when the note will actually sound). Stopped transport
    // pins the progression to its first step.
    auto progIndexAt = [&] (int s) -> int
    {
        if (! isPlaying || cfg.progStepCount <= 0 || cfg.progRateQn <= 0.0)
            return 0;
        const double qn = blockStartQn + (double) s / samplesPerQn;
        return wrapIndex ((juce::int64) std::floor (qn / cfg.progRateQn),
                          cfg.progStepCount);
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
                // Pitch is decided by the moment the note will sound (the
                // quantize target), so a deferred note lands on the
                // progression step in force when it plays, not when it was
                // played.
                const int target = quantActive ? quantTarget (s) : s;
                const int p = mapPitch (m.getNoteNumber(), root, scaleIndex,
                                        progIndexAt (target), cfg);
                forEachOutChannel (cfg.outChannelMask, m.getChannel(), [&] (int ch)
                {
                    if (quantActive)
                    {
                        pendingQuant.push_back ({ p, ch, m.getVelocity(),
                                                  target, s, -1,
                                                  m.getNoteNumber(), m.getChannel(), true });
                        return;
                    }
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
            // Key released while its quantized note-on is still waiting: keep
            // the played duration — the note now releases itself that long
            // after it finally sounds (activeGen), instead of via activePass.
            for (auto& q : pendingQuant)
                if (q.fromHost && q.gateSamples < 0
                    && q.inNote == m.getNoteNumber() && q.inChannel == m.getChannel())
                    q.gateSamples = juce::jmax (1, s - q.arrivalOffset);
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
        // remembering the note for its gate-timed release. With Quantize
        // active the note is deferred to its grid point instead (keeping its
        // gate), which is how a straight generator picks up the swing.
        auto emitGenerated = [&] (int rawPitch, int sample, int gateSamples)
        {
            const int target = quantActive ? quantTarget (sample) : sample;
            const int p = mapPitch (rawPitch, root, scaleIndex,
                                    progIndexAt (target), cfg);
            forEachOutChannel (cfg.outChannelMask, 1, [&] (int ch)
            {
                if (quantActive)
                {
                    pendingQuant.push_back ({ p, ch, 100, target, sample, gateSamples,
                                              0, 0, false });
                    return;
                }
                midi.addEvent (juce::MidiMessage::noteOn (ch, p, (juce::uint8) 100), sample);
                activeGen.push_back ({ p, ch, sample + gateSamples });
                scheduleEcho (p, ch, 100, sample);
            });
        };

        // Walk one module's grid across this block: a step fires at every
        // boundary k * stepQn inside [blockStartQn, blockEndQn), and `step`
        // receives the boundary's index counted from the song's bar 0 (which
        // is what locates repeat windows and cycles) plus its block sample.
        // The gate is capped one sample short of the step so even a 100%
        // gate's note-off can't collide with the next same-pitch note-on.
        auto runSteps = [&] (double stepQn, double gateFrac, auto&& step)
        {
            const double q           = juce::jmax (0.001, stepQn);
            const double stepSamples = juce::jmax (1.0, samplesPerQn * q);
            const int    gateSamples = juce::jlimit (1, juce::jmax (1, (int) stepSamples - 1),
                                                     (int) (stepSamples * gateFrac));
            // std::ceil, not integer truncation: a negative block start (host
            // pre-roll) must still round up to the boundary at or after it.
            for (auto k = (juce::int64) std::ceil (blockStartQn / q); ; ++k)
            {
                const double qn = (double) k * q;
                if (qn >= blockEndQn)
                    break;
                const int s = juce::jlimit (0, numSamples - 1,
                                            (int) std::llround ((qn - blockStartQn) * samplesPerQn));
                step (k, s, gateSamples);
            }
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

            runSteps (cfg.arpStepQn, cfg.arpGateFrac, [&] (juce::int64 k, int s, int gate)
            {
                if (seq.empty())
                    return;

                // The walk position is the grid index itself (modded into the
                // repeat window when one is set), so the phrase is a pure
                // function of the song position: identical on every host loop
                // pass, and re-joined mid-pattern when play starts mid-window.
                const juce::int64 i = window > 0 ? (juce::int64) wrapIndex (k, window) : k;

                const int n = (int) seq.size();
                int raw = seq.front();
                switch (cfg.arpMode)
                {
                    case ModuleOptions::kModeDown:
                        raw = seq[(size_t) (n - 1 - wrapIndex (i, n))];
                        break;
                    case ModuleOptions::kModeUpDown:
                    {
                        // Classic up-down: endpoints aren't doubled, so the
                        // cycle is 2n-2 steps (n > 1).
                        const int cycle = n > 1 ? 2 * n - 2 : 1;
                        const int j = wrapIndex (i, cycle);
                        raw = seq[(size_t) (j < n ? j : 2 * (n - 1) - j)];
                        break;
                    }
                    case ModuleOptions::kModeRandom:
                        raw = seq[(size_t) rng.nextInt (n)];
                        break;
                    default:   // kModeUp
                        raw = seq[(size_t) wrapIndex (i, n)];
                        break;
                }
                emitGenerated (raw, s, gate);
            });
        }

        if (cfg.hasRandom)
        {
            // All in-scale pitches inside the module's range; drawn uniformly.
            // Scale Off draws from all 12 chromatic pitches (the Chromatic scale
            // is exactly that set).
            const int rRoot  = cfg.randomRoot  >= 0 ? cfg.randomRoot  : root;
            const int rScale = cfg.randomScale == ModuleOptions::kScaleOff
                                   ? ModuleOptions::kChromaticScale
                                   : cfg.randomScale >= 0 ? cfg.randomScale : scaleIndex;
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

            runSteps (cfg.randomStepQn, cfg.randomGateFrac, [&] (juce::int64, int s, int gate)
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

            runSteps (cfg.scaleStepQn, cfg.scaleGateFrac, [&] (juce::int64 k, int s, int gate)
            {
                // Position inside the repeat window; steps past the pattern's
                // end are rests until the window wraps.
                const int idx = wrapIndex (k, stepsPerRepeat);
                if (idx < (int) pattern.size())
                    emitGenerated (pattern[(size_t) idx], s, gate);
            });
        }

        if (cfg.hasLfo)
        {
            const int lRoot  = cfg.lfoRoot  >= 0 ? cfg.lfoRoot  : root;
            // Scale Off maps chromatically (the Chromatic scale's 12 members).
            const int lScale = cfg.lfoScale == ModuleOptions::kScaleOff
                                   ? ModuleOptions::kChromaticScale
                                   : cfg.lfoScale >= 0 ? cfg.lfoScale : scaleIndex;
            const int centre = 48 + juce::jlimit (0, 11, lRoot);   // root at octave 3,
                                                                   // like the Scale gen
            // Depth in scale degrees: whole octaves are one scale-length each
            // (7 degrees in a 7-note scale, 12 in Chromatic) plus extra steps.
            const int degreesPerOctave = (int) ScaleTables::intervalsForScale (lScale).size();
            const int depthDegrees = juce::jmax (0, cfg.lfoDepthOct) * degreesPerOctave
                                   + juce::jmax (0, cfg.lfoDepthSteps);
            const double cycleQn = juce::jmax (0.001, cfg.lfoCycleQn);

            runSteps (cfg.lfoStepQn, cfg.lfoGateFrac, [&] (juce::int64 k, int s, int gate)
            {
                // Position inside the cycle, from the grid position in the
                // song plus the start-phase offset. x - floor(x) rather than
                // fmod so a negative (pre-roll) position still lands in
                // [0, 1).
                double x = (double) k * cfg.lfoStepQn / cycleQn + cfg.lfoPhase;
                x -= std::floor (x);

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

        if (cfg.hasChord)
        {
            // Build the chord: the (root, scale) stacked per chordType on the
            // chosen degree, from the shared generator centre (root at octave
            // 3), then the lowest tones raised an octave per the inversion.
            const int cRoot  = cfg.chordRoot  >= 0 ? cfg.chordRoot  : root;
            const int cScale = cfg.chordScale >= 0 ? cfg.chordScale : scaleIndex;
            const int base   = 48 + juce::jlimit (0, 11, cRoot);
            const int chordBase = ScaleTables::stepInScale (base, cRoot, cScale,
                                                            juce::jlimit (0, 6, cfg.chordDegree));

            std::vector<int> tones;
            for (int off : ModuleOptions::chordTypeDegrees (cfg.chordType))
                tones.push_back (ScaleTables::stepInScale (chordBase, cRoot, cScale, off));
            const int inv = juce::jlimit (0, (int) tones.size() - 1, cfg.chordInversion);
            for (int i = 0; i < inv; ++i)
                tones[(size_t) i] = juce::jmin (127, tones[(size_t) i] + 12);

            // A chord starts every period and sounds for length; runSteps caps
            // the gate one sample short of the period, so length >= period is
            // seamless legato. Emission goes through the normal generated path
            // (modulator chain, Quantize, Delay), one note per chord tone.
            // Repeat Endless (period 0) re-triggers back-to-back (period =
            // length), so the chord sounds continuously — the same "loops
            // back-to-back" reading Endless has on the stepped generators.
            const double periodQn = cfg.chordPeriodQn > 0.0
                                        ? cfg.chordPeriodQn
                                        : juce::jmax (0.001, cfg.chordLengthQn);
            const double gateFrac = juce::jlimit (0.0, 1.0, cfg.chordLengthQn / periodQn);
            runSteps (periodQn, gateFrac, [&] (juce::int64, int s, int gate)
            {
                for (int p : tones)
                    emitGenerated (p, s, gate);
            });
        }

        if (cfg.hasDrone)
        {
            const int dRoot  = cfg.droneRoot  >= 0 ? cfg.droneRoot  : root;
            const int dScale = cfg.droneScale >= 0 ? cfg.droneScale : scaleIndex;
            const int base   = juce::jlimit (0, 127,
                                             48 + juce::jlimit (0, 11, dRoot)
                                             + 12 * juce::jlimit (-ModuleOptions::kDroneOctaveRange,
                                                                  ModuleOptions::kDroneOctaveRange,
                                                                  cfg.droneOctave));
            std::vector<int> raw { base };
            switch (cfg.droneVoicing)
            {
                case ModuleOptions::kVoicingRootFifth:
                    // A perfect fifth snapped into the scale, so e.g. Locrian
                    // holds its diminished fifth instead of leaving the scale.
                    raw.push_back (ScaleTables::snapToScale (juce::jmin (127, base + 7),
                                                             dRoot, dScale));
                    break;
                case ModuleOptions::kVoicingRootOctave:
                    raw.push_back (juce::jmin (127, base + 12));
                    break;
                case ModuleOptions::kVoicingTriad:
                    raw.push_back (ScaleTables::stepInScale (base, dRoot, dScale, 2));
                    raw.push_back (ScaleTables::stepInScale (base, dRoot, dScale, 4));
                    break;
                default:   // kVoicingRoot
                    break;
            }

            // The pitches the drone should be sounding if it (re)started at
            // block sample `s`, mapped through the modulator chain. Mapping
            // can collapse two voicing tones onto one pitch — deduplicated so
            // a note-off can't be double-booked.
            auto mappedTones = [&] (int s)
            {
                std::vector<int> out;
                for (int r : raw)
                {
                    const int p = mapPitch (r, root, scaleIndex, progIndexAt (s), cfg);
                    if (std::find (out.begin(), out.end(), p) == out.end())
                        out.push_back (p);
                }
                return out;
            };

            // Release every held drone note at block sample `s`. Returns the
            // longest remaining hold time so a re-trigger can carry it over.
            auto releaseDrone = [&] (int s)
            {
                int remaining = 0;
                for (auto it = activeGen.begin(); it != activeGen.end();)
                {
                    if (it->drone)
                    {
                        remaining = juce::jmax (remaining, it->samplesLeft);
                        midi.addEvent (juce::MidiMessage::noteOff (it->channel, it->note), s);
                        it = activeGen.erase (it);
                    }
                    else
                        ++it;
                }
                return remaining;
            };

            auto startDrone = [&] (int s, int holdSamples)
            {
                for (int p : mappedTones (s))
                    forEachOutChannel (cfg.outChannelMask, 1, [&] (int ch)
                    {
                        midi.addEvent (juce::MidiMessage::noteOn (ch, p, (juce::uint8) 100), s);
                        activeGen.push_back ({ p, ch, s + holdSamples, true });
                    });
            };

            // Re-trigger on harmony change: if what the drone is holding no
            // longer matches what it should hold — a root/scale/voicing edit,
            // an Output channel change, or an upstream Progression step — the
            // old notes are released and the new ones start immediately,
            // keeping the remainder of the hold. A drone resting between
            // holds has nothing to re-trigger; the change simply shapes the
            // next period's notes.
            {
                std::vector<std::pair<int, int>> want;   // (pitch, channel)
                for (int p : mappedTones (0))
                    forEachOutChannel (cfg.outChannelMask, 1,
                                       [&] (int ch) { want.emplace_back (p, ch); });
                std::sort (want.begin(), want.end());

                std::vector<std::pair<int, int>> have;
                for (const auto& a : activeGen)
                    if (a.drone)
                        have.emplace_back (a.note, a.channel);
                std::sort (have.begin(), have.end());

                if (! have.empty() && want != have)
                    startDrone (0, releaseDrone (0));
            }

            // Period boundaries: a fresh hold starts, releasing first — in
            // linear playback the gate ended a sample earlier anyway, but a
            // host loop wrap can land a boundary mid-hold. The drone
            // deliberately bypasses Quantize and the Delay (see Engine.h).
            // Repeat Endless (period 0) re-triggers back-to-back, like the Chord.
            const double periodQn = cfg.dronePeriodQn > 0.0
                                        ? cfg.dronePeriodQn
                                        : juce::jmax (0.001, cfg.droneLengthQn);
            const double gateFrac = juce::jlimit (0.0, 1.0, cfg.droneLengthQn / periodQn);
            runSteps (periodQn, gateFrac, [&] (juce::int64, int s, int gate)
            {
                releaseDrone (s);
                startDrone (s, gate);
            });
        }
    }

    // --- Quantize: sound the deferred notes whose grid point lands this block --
    // Runs before the Delay sweep so echoes booked here can still fire within
    // the same block when the delay time is short.
    if (! cfg.hasQuantize)
    {
        // Module removed mid-wait: its buffered notes go with it (the shared
        // "buffered material is discarded" rule, same as the Delay below).
        pendingQuant.clear();
    }
    else if (! pendingQuant.empty())
    {
        for (auto& q : pendingQuant)
        {
            if (q.samplesUntil >= numSamples)
                continue;
            const int at = juce::jmax (0, q.samplesUntil);
            midi.addEvent (juce::MidiMessage::noteOn (q.channel, q.note,
                                                      (juce::uint8) q.velocity), at);
            if (q.gateSamples >= 0)
                activeGen.push_back ({ q.note, q.channel, q.samplesUntil + q.gateSamples });
            else
                activePass.push_back ({ q.inNote, q.inChannel, q.note, q.channel });
            scheduleEcho (q.note, q.channel, q.velocity, at);
            q.velocity = 0;   // mark as fired for the sweep below
        }
        pendingQuant.erase (std::remove_if (pendingQuant.begin(), pendingQuant.end(),
                                            [] (const QuantNote& q) { return q.velocity == 0; }),
                            pendingQuant.end());
        for (auto& q : pendingQuant)
        {
            q.samplesUntil  -= numSamples;
            q.arrivalOffset -= numSamples;
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

    // --- Humanize: final-stage feel warp over the whole outgoing stream -------
    // Runs last, so it shapes everything already in the buffer: pass-through,
    // generated, quantized, and delayed notes alike. Only warps while playing —
    // stopped (or with the module removed) it passes events straight through for
    // immediate live feel, but still flushes any buffered note-offs at sample 0
    // so a note whose off was in flight can't hang.
    const bool humanizeActive = cfg.hasHumanize && isPlaying;
    if (! humanizeActive)
    {
        if (! pendingHuman.empty())
        {
            for (const auto& e : pendingHuman)
                if (! e.msg.isNoteOn())   // release / CC — dropping these could hang a note
                    midi.addEvent (e.msg, 0);
            pendingHuman.clear();
        }
        humanHeld.clear();
    }
    else
    {
        const double stepQn  = juce::jmax (0.001, cfg.humanizeStepQn);
        const double swingQn = cfg.humanizeSwing * 0.5 * stepQn;
        const double layQn   = cfg.humanizeLayback * kHumanizeLaybackFrac * stepQn;

        // Pull the block's straight output aside and rebuild it warped.
        juce::MidiBuffer straight;
        straight.swapWith (midi);

        auto emitOrDefer = [&] (const juce::MidiMessage& msg, int newSample)
        {
            if (newSample < numSamples)
                midi.addEvent (msg, juce::jmax (0, newSample));
            else
                pendingHuman.push_back ({ msg, newSample });
        };

        // 1) Fire buffered events whose warped time lands in this block; the
        //    rest are decremented at the end (like pendingEchoes / pendingQuant).
        for (auto& e : pendingHuman)
            if (e.samplesUntil < numSamples)
            {
                midi.addEvent (e.msg, juce::jmax (0, e.samplesUntil));
                e.samplesUntil = std::numeric_limits<int>::min();   // mark fired
            }
        pendingHuman.erase (std::remove_if (pendingHuman.begin(), pendingHuman.end(),
                                            [] (const HumanEvent& e)
                                            { return e.samplesUntil == std::numeric_limits<int>::min(); }),
                            pendingHuman.end());

        // 2) Warp this block's fresh events. The swing + lay-back offset is a
        //    pure function of song position (applied to every note event, on and
        //    off, so durations follow the groove); note-ons additionally take a
        //    random late nudge and velocity shaping, note-offs a random
        //    lengthening — all delay-only, so nothing ever moves before it
        //    arrived. A note-off reuses its on's timing jitter (kept in
        //    humanHeld) so the jitter never changes the note's length.
        for (const auto meta : straight)
        {
            const auto msg = meta.getMessage();
            const int  s   = meta.samplePosition;
            const double qn       = blockStartQn + (double) s / samplesPerQn;
            const double warpedQn = swingWarpQn (qn, stepQn, swingQn) + layQn;
            const juce::int64 gi  = (juce::int64) std::floor (qn / stepQn);
            auto sampleForQn = [&] (double targetQn)
            {
                return juce::jmax (s, (int) std::llround ((targetQn - blockStartQn) * samplesPerQn));
            };

            if (msg.isNoteOn())
            {
                const int chan  = msg.getChannel();
                const int pitch = msg.getNoteNumber();
                const int key   = pitch * 16 + chan;

                const double jitterQn = hash01 (gi, key, kSaltTime)
                                            * cfg.humanizeTimeJit * kHumanizeTimeJitFrac * stepQn;

                // Accent: strong beats (even step of the swing pair) louder,
                // weak beats softer; then a symmetric random touch on top.
                const bool   strong = (gi & 1) == 0;
                const double accent = 1.0 + cfg.humanizeAccent * kHumanizeAccentDepth
                                                * (strong ? 1.0 : -1.0);
                const double velJit = (hash01 (gi, key, kSaltVel) * 2.0 - 1.0)
                                          * cfg.humanizeVelJit * kHumanizeVelRange;
                const int newVel = juce::jlimit (1, 127,
                                                 (int) std::llround ((double) msg.getVelocity() * accent + velJit));

                humanHeld.push_back ({ chan, pitch, jitterQn });
                emitOrDefer (juce::MidiMessage::noteOn (chan, pitch, (juce::uint8) newVel),
                             sampleForQn (warpedQn + jitterQn));
            }
            else if (msg.isNoteOff())
            {
                // Reuse the matching on's timing jitter so the note keeps its
                // length; add an independent one-sided lengthening on top.
                double onJitterQn = 0.0;
                for (auto it = humanHeld.begin(); it != humanHeld.end(); ++it)
                    if (it->channel == msg.getChannel() && it->note == msg.getNoteNumber())
                    {
                        onJitterQn = it->jitterQn;
                        humanHeld.erase (it);
                        break;
                    }
                const double lenQn = hash01 (gi, msg.getNoteNumber() * 16 + msg.getChannel(), kSaltLen)
                                         * cfg.humanizeLenJit * kHumanizeLenJitFrac * stepQn;
                emitOrDefer (msg, sampleForQn (warpedQn + onJitterQn + lenQn));
            }
            else
            {
                // CC / pitch-bend / clock / sysex: left on their original sample
                // (shifting a controller or clock off its note would do more harm
                // than the groove is worth).
                midi.addEvent (msg, s);
            }
        }

        // 3) Age the still-buffered events into the next block.
        for (auto& e : pendingHuman)
            e.samplesUntil -= numSamples;
    }

}
