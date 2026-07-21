#include "Engine.h"
#include "ScaleTables.h"
#include "ModuleSettings.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    // Echoes quieter than this aren't scheduled — the velocity decay is what
    // terminates the Delay's feedback chain.
    constexpr int kMinEchoVelocity = 5;

    // Generated notes' fixed velocity (an Output or downstream module may
    // reshape it; the generators themselves have no velocity control yet).
    constexpr int kGenVelocity = 100;

    // Generators emit on this channel; an Output node restamps on the way out.
    constexpr int kGenChannel = 1;

    // Mathematical mod for grid indices: a negative position (host pre-roll /
    // count-in) must wrap into the pattern, not mirror around zero as C's %
    // would.
    int wrapIndex (juce::int64 i, int n)
    {
        const juce::int64 m = i % (juce::int64) n;
        return (int) (m < 0 ? m + n : m);
    }

    // Signed count of scale steps from `a` to `b` (both snapped into the scale
    // first), positive when `b` is above `a`. Used by Mirror's diatonic
    // reflection. Bounded walk — pitches live in 0..127, so the count can't
    // exceed ~75 for any real scale; the guard caps a pathological one.
    int scaleStepsBetween (int a, int b, int root, int scaleIndex)
    {
        a = ScaleTables::snapToScale (juce::jlimit (0, 127, a), root, scaleIndex);
        b = ScaleTables::snapToScale (juce::jlimit (0, 127, b), root, scaleIndex);
        if (a == b)
            return 0;
        const int dir = b > a ? 1 : -1;
        int p = a, count = 0;
        for (int guard = 0; p != b && guard < 256; ++guard)
        {
            int q = p + dir;
            while (q >= 0 && q <= 127 && ! ScaleTables::isInScale (q, root, scaleIndex))
                q += dir;
            if (q < 0 || q > 127)
                break;
            p = q;
            count += dir;
        }
        return count;
    }

    // Reflect `note` around `pivot`. Chromatic mode is a plain semitone mirror
    // (an interval up becomes that interval down); diatonic mode mirrors by
    // scale degrees — the reflected note sits the same number of scale steps on
    // the far side of the pivot — so a fold or invert stays in key. Mirror uses
    // this for both the centre inversion and the boundary fold.
    int reflectAround (int note, int pivot, int root, int scaleIndex, bool chromatic)
    {
        if (chromatic)
            return 2 * pivot - note;
        const int d = scaleStepsBetween (pivot, note, root, scaleIndex);
        return ScaleTables::stepInScale (pivot, root, scaleIndex, -d);
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
    // and Strum's "random" jitter is a deterministic function of song position
    // (grid index) and note — the humanized feel then repeats identically on
    // every host loop pass instead of shimmering. `salt` separates the
    // independent draws. A finalizer-style integer hash (splitmix64-ish).
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

    // Strum's chord-detection window: note-ons arriving within this of the
    // group's first note are treated as one chord. It is the (small, fixed)
    // latency Strum adds, since the fan order can't be decided until the whole
    // chord is in — kept short enough to feel immediate, long enough to catch a
    // hand-played chord roll.
    constexpr double kStrumGroupWindowSec = 0.03;   // 30 ms
    // Full-jitter maxima: the random late nudge (as a share of the per-note
    // spread gap) and the velocity wobble, reached at the 100% Jitter setting.
    constexpr double kStrumJitterTimeFrac = 0.5;    // up to half a gap, late
    constexpr double kStrumJitterVelRange = 20.0;   // +/-20 velocity
    constexpr double kStrumVelTiltDepth   = 0.6;    // full tilt = +/-60% across the fan
    constexpr int    kStrumSaltTime = 11, kStrumSaltVel = 12;

    // Emit note-offs for everything in a gate list (transport-stop flush /
    // module cleanup) and clear it.
    void flushActive (std::vector<Engine::ActiveNote>& act, juce::MidiBuffer& out, int sample)
    {
        for (const auto& a : act)
            out.addEvent (juce::MidiMessage::noteOff (a.channel, a.note), sample);
        act.clear();
    }

    // Release the gate-list notes whose countdown ends inside this block; age
    // the rest into the next block.
    void releaseDueGates (std::vector<Engine::ActiveNote>& act, juce::MidiBuffer& out, int numSamples)
    {
        for (auto it = act.begin(); it != act.end();)
        {
            if (it->samplesLeft < numSamples)
            {
                out.addEvent (juce::MidiMessage::noteOff (it->channel, it->note),
                              juce::jmax (0, it->samplesLeft));
                it = act.erase (it);
            }
            else
            {
                it->samplesLeft -= numSamples;
                ++it;
            }
        }
    }

    // Remove the first (channel, note) match from a key list; true if found.
    bool eraseKey (std::vector<Engine::KeyRef>& keys, int channel, int note)
    {
        for (auto it = keys.begin(); it != keys.end(); ++it)
            if (it->channel == channel && it->note == note)
            {
                keys.erase (it);
                return true;
            }
        return false;
    }

    // Walk one module's grid across this block: a step fires at every boundary
    // k * stepQn inside [blockStartQn, blockEndQn), and `step` receives the
    // boundary's index counted from the song's bar 0 (which is what locates
    // repeat windows and cycles) plus its block sample. The gate is capped one
    // sample short of the step so even a 100% gate's note-off can't collide
    // with the next same-pitch note-on.
    template <typename Fn>
    void runSteps (const Engine::BlockContext& ctx, double stepQn, double gateFrac, Fn&& step)
    {
        const double q           = juce::jmax (0.001, stepQn);
        const double stepSamples = juce::jmax (1.0, ctx.samplesPerQn * q);
        const int    gateSamples = juce::jlimit (1, juce::jmax (1, (int) stepSamples - 1),
                                                 (int) (stepSamples * gateFrac));
        // std::ceil, not integer truncation: a negative block start (host
        // pre-roll) must still round up to the boundary at or after it.
        for (auto k = (juce::int64) std::ceil (ctx.blockStartQn / q); ; ++k)
        {
            const double qn = (double) k * q;
            if (qn >= ctx.blockEndQn)
                break;
            const int s = juce::jlimit (0, ctx.numSamples - 1,
                                        (int) std::llround ((qn - ctx.blockStartQn) * ctx.samplesPerQn));
            step (k, s, gateSamples);
        }
    }

    // How many steps fit in a repeat window; 0 = Endless (no window).
    int stepsPerWindow (double repeatQn, double stepQn)
    {
        return repeatQn > 0.0
            ? juce::jmax (1, (int) std::llround (repeatQn / juce::jmax (0.001, stepQn)))
            : 0;
    }

    // The shared pitch-mapper skeleton: map each note-on's pitch, remember
    // (in -> out) in `sounding`, release the remembered pitch on the note-off.
    // mapAt(note, sample) returns the mapped pitch or -1 to drop the note
    // (nothing emitted, nothing booked — Mirror's Limit mode). A note-off with
    // no remembered entry is swallowed (its on was dropped or predates this
    // node), so no stray release travels downstream.
    template <typename MapFn>
    void mapNoteStream (const juce::MidiBuffer& in, juce::MidiBuffer& out,
                        std::vector<Engine::SoundingNote>& sounding, MapFn&& mapAt)
    {
        for (const auto meta : in)
        {
            const auto m = meta.getMessage();
            const int  s = meta.samplePosition;
            if (m.isNoteOn())
            {
                const int p = mapAt (m.getNoteNumber(), s);
                if (p < 0)
                    continue;
                out.addEvent (juce::MidiMessage::noteOn (m.getChannel(), p,
                                                         (juce::uint8) m.getVelocity()), s);
                sounding.push_back ({ m.getChannel(), m.getNoteNumber(), m.getChannel(), p });
            }
            else if (m.isNoteOff())
            {
                for (auto it = sounding.begin(); it != sounding.end(); ++it)
                    if (it->inChannel == m.getChannel() && it->inNote == m.getNoteNumber())
                    {
                        out.addEvent (juce::MidiMessage::noteOff (it->outChannel, it->outNote), s);
                        sounding.erase (it);
                        break;
                    }
            }
            else
            {
                out.addEvent (m, s);
            }
        }
    }
}

void Engine::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void Engine::reset()
{
    states.clear();
    hostSounding.clear();
    lastTopologyVersion = -1;
    fallbackQn = 0.0;
    wasPlaying = false;
}

void Engine::process (juce::MidiBuffer& midi,
                      int numSamples,
                      const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos,
                      int root, int scaleIndex,
                      const GraphSnapshot* graph)
{
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

    // Transport start: nothing to rewind — every grid is derived from the song
    // position — but the ppq fallback re-anchors to zero so a host that
    // reports no ppq still counts its grids from the moment play was pressed.
    if (isPlaying && ! wasPlaying && ! hostPpq.hasValue())
        fallbackQn = 0.0;

    // The block's song position in quarter notes, owning the half-open range
    // [blockStartQn, blockEndQn): a grid boundary exactly on the block start
    // fires here, one exactly on the end fires next block — so a host loop
    // wrap landing on a boundary can neither double-fire nor skip it.
    const double blockStartQn = hostPpq.hasValue() ? *hostPpq : fallbackQn;
    const double blockEndQn   = blockStartQn + (double) numSamples / samplesPerQn;
    // The fallback tracks the block end even while the host is supplying ppq,
    // so a host that stops reporting it mid-run continues seamlessly.
    if (isPlaying)
        fallbackQn = blockEndQn;

    const bool justStopped = ! isPlaying && wasPlaying;
    wasPlaying = isPlaying;

    // Publish the resolved clock for UI playhead displays (Rhythmize's step
    // grid) — written before the empty-graph early-outs so the playhead runs
    // whatever the patch looks like.
    uiSongQn.store (blockStartQn, std::memory_order_relaxed);
    uiPlaying.store (isPlaying, std::memory_order_relaxed);

    // Capture the host's incoming events; the buffer is rebuilt from the
    // Output nodes' streams.
    juce::MidiBuffer incoming;
    incoming.swapWith (midi);

    // Empty graph (or none yet): silence — but first release anything still
    // sounding at the host, so deleting the last module can't hang a note.
    if (graph == nullptr || graph->nodes.empty())
    {
        for (const auto& k : hostSounding)
            midi.addEvent (juce::MidiMessage::noteOff (k.channel, k.note), 0);
        hostSounding.clear();
        states.clear();
        if (graph != nullptr)
            lastTopologyVersion = graph->topologyVersion;
        return;
    }

    // Patch rewired (module or cable added/removed): release everything and
    // start from clean per-node state. Coarse but safe — per-node in-flight
    // bookkeeping can't be trusted across an arbitrary rewire, and the flush
    // guarantees no hanging notes whatever changed. Settings edits keep the
    // version, so they never cut sounding notes.
    if (graph->topologyVersion != lastTopologyVersion)
    {
        for (const auto& k : hostSounding)
            midi.addEvent (juce::MidiMessage::noteOff (k.channel, k.note), 0);
        hostSounding.clear();
        states.clear();
        lastTopologyVersion = graph->topologyVersion;
    }

    const BlockContext ctx { numSamples, isPlaying, justStopped,
                             blockStartQn, blockEndQn, samplesPerQn,
                             root, scaleIndex };

    const int n = (int) graph->nodes.size();
    if ((int) nodeBuf.size() < n)
        nodeBuf.resize ((size_t) n);

    juce::MidiBuffer inBuf;
    for (int i = 0; i < n; ++i)
    {
        const auto& node = graph->nodes[(size_t) i];
        nodeBuf[(size_t) i].clear();

        inBuf.clear();
        if (node.type == ModuleType::MidiIn)
            inBuf.addEvents (incoming, 0, -1, 0);
        else
            for (int u : node.inputs)
                if (u >= 0 && u < i)   // topological order guarantee, defensively checked
                    inBuf.addEvents (nodeBuf[(size_t) u], 0, -1, 0);

        processNode (node, states[node.id], inBuf, nodeBuf[(size_t) i], ctx);
    }

    for (int i = 0; i < n; ++i)
        if (graph->nodes[(size_t) i].type == ModuleType::Output)
            midi.addEvents (nodeBuf[(size_t) i], 0, -1, 0);

    // Track what is now sounding at the host, for the topology-change flush.
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            hostSounding.push_back ({ m.getChannel(), m.getNoteNumber() });
        else if (m.isNoteOff())
            eraseKey (hostSounding, m.getChannel(), m.getNoteNumber());
    }
}

void Engine::processNode (const GraphNode& node, NodeState& st,
                          const juce::MidiBuffer& in, juce::MidiBuffer& out,
                          const BlockContext& ctx)
{
    switch (node.type)
    {
        case ModuleType::MidiIn:      processMidiIn      (node, st, in, out, ctx); break;
        case ModuleType::Output:      processOutput      (node, st, in, out, ctx); break;
        case ModuleType::Random:      processRandom      (node, st, in, out, ctx); break;
        case ModuleType::ScaleGen:    processScaleGen    (node, st, in, out, ctx); break;
        case ModuleType::Lfo:         processLfo         (node, st, in, out, ctx); break;
        case ModuleType::Chord:       processChord       (node, st, in, out, ctx); break;
        case ModuleType::Drone:       processDrone       (node, st, in, out, ctx); break;
        case ModuleType::Arp:         processArp         (node, st, in, out, ctx); break;
        case ModuleType::Rhythmize:   processRhythmize   (node, st, in, out, ctx); break;
        case ModuleType::Harmonizer:  processHarmonizer  (node, st, in, out, ctx); break;
        case ModuleType::ScaleMod:    processScaleMod    (node, st, in, out, ctx); break;
        case ModuleType::Progression: processProgression (node, st, in, out, ctx); break;
        case ModuleType::Shift:       processShift       (node, st, in, out, ctx); break;
        case ModuleType::Mirror:      processMirror      (node, st, in, out, ctx); break;
        case ModuleType::Quantize:    processQuantize    (node, st, in, out, ctx); break;
        case ModuleType::Delay:       processDelay       (node, st, in, out, ctx); break;
        case ModuleType::Strum:       processStrum       (node, st, in, out, ctx); break;
        case ModuleType::Humanize:    processHumanize    (node, st, in, out, ctx); break;
    }
}

// --- I/O ---------------------------------------------------------------------

void Engine::processMidiIn (const GraphNode& node, NodeState& st,
                            const juce::MidiBuffer& in, juce::MidiBuffer& out,
                            const BlockContext&)
{
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int  s = meta.samplePosition;

        if (m.getChannel() <= 0)   // clock / sysex: not channel-filtered
        {
            out.addEvent (m, s);
            continue;
        }

        const bool match = node.params.channel == 0
                            || m.getChannel() == node.params.channel;
        if (m.isNoteOn())
        {
            if (! match)
                continue;
            st.inHeld.push_back ({ m.getChannel(), m.getNoteNumber() });
            out.addEvent (m, s);
        }
        else if (m.isNoteOff())
        {
            // Admitted iff its note-on was, so a channel edit mid-note can
            // neither hang the note nor leak a stray release.
            if (eraseKey (st.inHeld, m.getChannel(), m.getNoteNumber()))
                out.addEvent (m, s);
        }
        else if (match)
        {
            out.addEvent (m, s);
        }
    }
}

void Engine::processOutput (const GraphNode& node, NodeState& st,
                            const juce::MidiBuffer& in, juce::MidiBuffer& out,
                            const BlockContext&)
{
    const int outCh = juce::jlimit (1, 16, node.params.channel);
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int  s = meta.samplePosition;

        if (m.getChannel() <= 0)
        {
            out.addEvent (m, s);
            continue;
        }
        if (m.isNoteOn())
        {
            out.addEvent (juce::MidiMessage::noteOn (outCh, m.getNoteNumber(),
                                                     (juce::uint8) m.getVelocity()), s);
            st.sounding.push_back ({ m.getChannel(), m.getNoteNumber(), outCh, m.getNoteNumber() });
        }
        else if (m.isNoteOff())
        {
            // Release on the channel the note-on was stamped with, even if the
            // user edited the Output channel mid-note.
            int ch = outCh;
            for (auto it = st.sounding.begin(); it != st.sounding.end(); ++it)
                if (it->inChannel == m.getChannel() && it->inNote == m.getNoteNumber())
                {
                    ch = it->outChannel;
                    st.sounding.erase (it);
                    break;
                }
            out.addEvent (juce::MidiMessage::noteOff (ch, m.getNoteNumber()), s);
        }
        else
        {
            auto copy = m;
            copy.setChannel (outCh);
            out.addEvent (copy, s);
        }
    }
}

// --- Generators --------------------------------------------------------------

void Engine::processRandom (const GraphNode& node, NodeState& st,
                            const juce::MidiBuffer&, juce::MidiBuffer& out,
                            const BlockContext& ctx)
{
    if (ctx.justStopped)
        flushActive (st.activeGen, out, 0);

    if (ctx.isPlaying)
    {
        const auto& p = node.params;
        // All in-scale pitches inside the module's range; drawn uniformly.
        // Scale Off draws from all 12 chromatic pitches (the Chromatic scale
        // is exactly that set).
        const int rRoot  = p.root >= 0 ? p.root : ctx.root;
        const int rScale = p.scale == ModuleOptions::kScaleOff
                               ? ModuleOptions::kChromaticScale
                               : p.scale >= 0 ? p.scale : ctx.scaleIndex;
        const int lo = juce::jlimit (0, 127, juce::jmin (p.rangeFrom, p.rangeTo));
        const int hi = juce::jlimit (0, 127, juce::jmax (p.rangeFrom, p.rangeTo));

        std::vector<int> candidates;
        for (int nn = lo; nn <= hi; ++nn)
            if (ScaleTables::isInScale (nn, rRoot, rScale))
                candidates.push_back (nn);
        // A degenerate range can miss the scale entirely (e.g. from == to on a
        // non-scale pitch); snap rather than fall silent.
        if (candidates.empty())
            candidates.push_back (ScaleTables::snapToScale ((lo + hi) / 2, rRoot, rScale));

        runSteps (ctx, p.stepQn, p.gateFrac, [&] (juce::int64, int s, int gate)
        {
            const int pick = candidates[(size_t) rng.nextInt ((int) candidates.size())];
            out.addEvent (juce::MidiMessage::noteOn (kGenChannel, pick, (juce::uint8) kGenVelocity), s);
            st.activeGen.push_back ({ pick, kGenChannel, s + gate });
        });
    }

    releaseDueGates (st.activeGen, out, ctx.numSamples);
}

void Engine::processScaleGen (const GraphNode& node, NodeState& st,
                              const juce::MidiBuffer&, juce::MidiBuffer& out,
                              const BlockContext& ctx)
{
    if (ctx.justStopped)
        flushActive (st.activeGen, out, 0);

    if (ctx.isPlaying)
    {
        const auto& p = node.params;
        // Build the pattern: the scale walked from the root at octave 3
        // (MIDI 48 + root) across `octaves`, optionally capped with the octave
        // root; Down plays the same notes reversed.
        const int sRoot  = p.root  >= 0 ? p.root  : ctx.root;
        const int sScale = p.scale >= 0 ? p.scale : ctx.scaleIndex;
        const int base   = 48 + juce::jlimit (0, 11, sRoot);
        const int octs   = juce::jlimit (1, 4, p.octaves);

        std::vector<int> pattern;
        for (int o = 0; o < octs; ++o)
            for (int iv : ScaleTables::intervalsForScale (sScale))
                pattern.push_back (juce::jlimit (0, 127, base + o * 12 + iv));
        if (p.endOnRoot)
            pattern.push_back (juce::jlimit (0, 127, base + octs * 12));
        if (p.mode == ModuleOptions::kModeDown)
            std::reverse (pattern.begin(), pattern.end());

        // Endless (window 0) loops the pattern back-to-back: the window is
        // simply the pattern's own length.
        const int window = stepsPerWindow (p.repeatQn, p.stepQn);
        const int stepsPerRepeat = window > 0 ? window : (int) pattern.size();

        runSteps (ctx, p.stepQn, p.gateFrac, [&] (juce::int64 k, int s, int gate)
        {
            // Position inside the repeat window; steps past the pattern's end
            // are rests until the window wraps.
            const int idx = wrapIndex (k, stepsPerRepeat);
            if (idx < (int) pattern.size())
            {
                const int pick = pattern[(size_t) idx];
                out.addEvent (juce::MidiMessage::noteOn (kGenChannel, pick, (juce::uint8) kGenVelocity), s);
                st.activeGen.push_back ({ pick, kGenChannel, s + gate });
            }
        });
    }

    releaseDueGates (st.activeGen, out, ctx.numSamples);
}

void Engine::processLfo (const GraphNode& node, NodeState& st,
                         const juce::MidiBuffer&, juce::MidiBuffer& out,
                         const BlockContext& ctx)
{
    if (ctx.justStopped)
        flushActive (st.activeGen, out, 0);

    if (ctx.isPlaying)
    {
        const auto& p = node.params;
        const int lRoot  = p.root >= 0 ? p.root : ctx.root;
        // Scale Off maps chromatically (the Chromatic scale's 12 members).
        const int lScale = p.scale == ModuleOptions::kScaleOff
                               ? ModuleOptions::kChromaticScale
                               : p.scale >= 0 ? p.scale : ctx.scaleIndex;
        const int centre = 48 + juce::jlimit (0, 11, lRoot);   // root at octave 3,
                                                               // like the Scale gen
        // Depth in scale degrees: whole octaves are one scale-length each
        // (7 degrees in a 7-note scale, 12 in Chromatic) plus extra steps.
        const int degreesPerOctave = (int) ScaleTables::intervalsForScale (lScale).size();
        const int depthDegrees = juce::jmax (0, p.lfoDepthOct) * degreesPerOctave
                               + juce::jmax (0, p.lfoDepthSteps);
        const double cycleQn = juce::jmax (0.001, p.lfoCycleQn);

        runSteps (ctx, p.stepQn, p.gateFrac, [&] (juce::int64 k, int s, int gate)
        {
            // Position inside the cycle, from the grid position in the song
            // plus the start-phase offset. x - floor(x) rather than fmod so a
            // negative (pre-roll) position still lands in [0, 1).
            double x = (double) k * p.stepQn / cycleQn + p.lfoPhase;
            x -= std::floor (x);

            double v = 0.0;   // bipolar shape value at x
            switch (p.lfoShape)
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
            const int pick = ScaleTables::stepInScale (centre, lRoot, lScale, offset);
            out.addEvent (juce::MidiMessage::noteOn (kGenChannel, pick, (juce::uint8) kGenVelocity), s);
            st.activeGen.push_back ({ pick, kGenChannel, s + gate });
        });
    }

    releaseDueGates (st.activeGen, out, ctx.numSamples);
}

void Engine::processChord (const GraphNode& node, NodeState& st,
                           const juce::MidiBuffer&, juce::MidiBuffer& out,
                           const BlockContext& ctx)
{
    if (ctx.justStopped)
        flushActive (st.activeGen, out, 0);

    if (ctx.isPlaying)
    {
        const auto& p = node.params;
        // Build the chord: the (root, scale) stacked per chordType on the
        // chosen degree, from the shared generator centre (root at octave 3),
        // then the lowest tones raised an octave per the inversion.
        const int cRoot  = p.root  >= 0 ? p.root  : ctx.root;
        const int cScale = p.scale >= 0 ? p.scale : ctx.scaleIndex;
        const int base   = 48 + juce::jlimit (0, 11, cRoot);
        const int chordBase = ScaleTables::stepInScale (base, cRoot, cScale,
                                                        juce::jlimit (0, 6, p.chordDegree));

        std::vector<int> tones;
        for (int off : ModuleOptions::chordTypeDegrees (p.chordType))
            tones.push_back (ScaleTables::stepInScale (chordBase, cRoot, cScale, off));
        const int inv = juce::jlimit (0, (int) tones.size() - 1, p.chordInversion);
        for (int i = 0; i < inv; ++i)
            tones[(size_t) i] = juce::jmin (127, tones[(size_t) i] + 12);

        // A chord starts every period and sounds for length; runSteps caps the
        // gate one sample short of the period, so length >= period is seamless
        // legato. Repeat Endless (period 0) re-triggers back-to-back.
        const double periodQn = p.holdPeriodQn > 0.0
                                    ? p.holdPeriodQn
                                    : juce::jmax (0.001, p.holdLengthQn);
        const double gateFrac = juce::jlimit (0.0, 1.0, p.holdLengthQn / periodQn);
        runSteps (ctx, periodQn, gateFrac, [&] (juce::int64, int s, int gate)
        {
            for (int t : tones)
            {
                out.addEvent (juce::MidiMessage::noteOn (kGenChannel, t, (juce::uint8) kGenVelocity), s);
                st.activeGen.push_back ({ t, kGenChannel, s + gate });
            }
        });
    }

    releaseDueGates (st.activeGen, out, ctx.numSamples);
}

void Engine::processDrone (const GraphNode& node, NodeState& st,
                           const juce::MidiBuffer&, juce::MidiBuffer& out,
                           const BlockContext& ctx)
{
    if (ctx.justStopped)
        flushActive (st.activeGen, out, 0);

    if (ctx.isPlaying)
    {
        const auto& p = node.params;
        const int dRoot  = p.root  >= 0 ? p.root  : ctx.root;
        const int dScale = p.scale >= 0 ? p.scale : ctx.scaleIndex;
        const int base   = juce::jlimit (0, 127,
                                         48 + juce::jlimit (0, 11, dRoot)
                                         + 12 * juce::jlimit (-ModuleOptions::kDroneOctaveRange,
                                                              ModuleOptions::kDroneOctaveRange,
                                                              p.droneOctave));
        // The voicing's pitches, deduplicated (a small scale can collapse two
        // voicing tones onto one pitch, which would double-book a note-off).
        std::vector<int> tones { base };
        switch (p.droneVoicing)
        {
            case ModuleOptions::kVoicingRootFifth:
                // A perfect fifth snapped into the scale, so e.g. Locrian
                // holds its diminished fifth instead of leaving the scale.
                tones.push_back (ScaleTables::snapToScale (juce::jmin (127, base + 7),
                                                           dRoot, dScale));
                break;
            case ModuleOptions::kVoicingRootOctave:
                tones.push_back (juce::jmin (127, base + 12));
                break;
            case ModuleOptions::kVoicingTriad:
                tones.push_back (ScaleTables::stepInScale (base, dRoot, dScale, 2));
                tones.push_back (ScaleTables::stepInScale (base, dRoot, dScale, 4));
                break;
            default:   // kVoicingRoot
                break;
        }
        std::sort (tones.begin(), tones.end());
        tones.erase (std::unique (tones.begin(), tones.end()), tones.end());

        // Release every held drone note at block sample `s`. Returns the
        // longest remaining hold time so a re-trigger can carry it over.
        auto releaseDrone = [&] (int s)
        {
            int remaining = 0;
            for (auto it = st.activeGen.begin(); it != st.activeGen.end();)
            {
                if (it->drone)
                {
                    remaining = juce::jmax (remaining, it->samplesLeft);
                    out.addEvent (juce::MidiMessage::noteOff (it->channel, it->note), s);
                    it = st.activeGen.erase (it);
                }
                else
                    ++it;
            }
            return remaining;
        };

        auto startDrone = [&] (int s, int holdSamples)
        {
            for (int t : tones)
            {
                out.addEvent (juce::MidiMessage::noteOn (kGenChannel, t, (juce::uint8) kGenVelocity), s);
                st.activeGen.push_back ({ t, kGenChannel, s + holdSamples, true });
            }
        };

        // Re-trigger on harmony change: if what the drone is holding no longer
        // matches what it should hold — a root/scale/voicing/octave edit — the
        // old notes are released and the new ones start immediately, keeping
        // the remainder of the hold. A drone resting between holds has nothing
        // to re-trigger; the change simply shapes the next period's notes.
        {
            std::vector<int> have;
            for (const auto& a : st.activeGen)
                if (a.drone)
                    have.push_back (a.note);
            std::sort (have.begin(), have.end());
            if (! have.empty() && have != tones)
                startDrone (0, releaseDrone (0));
        }

        // Period boundaries: a fresh hold starts, releasing first — in linear
        // playback the gate ended a sample earlier anyway, but a host loop
        // wrap can land a boundary mid-hold. Repeat Endless (period 0)
        // re-triggers back-to-back, like the Chord.
        const double periodQn = p.holdPeriodQn > 0.0
                                    ? p.holdPeriodQn
                                    : juce::jmax (0.001, p.holdLengthQn);
        const double gateFrac = juce::jlimit (0.0, 1.0, p.holdLengthQn / periodQn);
        runSteps (ctx, periodQn, gateFrac, [&] (juce::int64, int s, int gate)
        {
            releaseDrone (s);
            startDrone (s, gate);
        });
    }

    releaseDueGates (st.activeGen, out, ctx.numSamples);
}

// --- Input transformers ------------------------------------------------------

void Engine::processArp (const GraphNode& node, NodeState& st,
                         const juce::MidiBuffer& in, juce::MidiBuffer& out,
                         const BlockContext& ctx)
{
    if (ctx.justStopped)
        flushActive (st.activeGen, out, 0);

    // Input notes are the arp's data: consumed while playing (they don't also
    // pass through), passed through while stopped so live playing stays
    // audible. `passedOn` remembers which ons were passed, so exactly those
    // offs pass too — a note passed while stopped releases cleanly even if
    // the transport starts (and swallowing begins) mid-note.
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int  s = meta.samplePosition;
        if (m.isNoteOn())
        {
            st.held[(size_t) m.getNoteNumber()] = true;
            if (! ctx.isPlaying)
            {
                out.addEvent (m, s);
                st.passedOn.push_back ({ m.getChannel(), m.getNoteNumber() });
            }
        }
        else if (m.isNoteOff())
        {
            st.held[(size_t) m.getNoteNumber()] = false;
            if (eraseKey (st.passedOn, m.getChannel(), m.getNoteNumber()))
                out.addEvent (m, s);
        }
        else
        {
            out.addEvent (m, s);
        }
    }

    if (ctx.isPlaying)
    {
        const auto& p = node.params;
        // The walk sequence: held notes ascending, repeated per octave of the
        // span (the classic arp octave extension).
        const int octs = juce::jlimit (1, 4, p.octaves);
        std::vector<int> seq;
        for (int o = 0; o < octs; ++o)
            for (int nn = 0; nn < 128; ++nn)
                if (st.held[(size_t) nn])
                    seq.push_back (juce::jmin (127, nn + o * 12));

        const int window = stepsPerWindow (p.repeatQn, p.stepQn);

        runSteps (ctx, p.stepQn, p.gateFrac, [&] (juce::int64 k, int s, int gate)
        {
            if (seq.empty())
                return;

            // The walk position is the grid index itself (modded into the
            // repeat window when one is set), so the phrase is a pure function
            // of the song position: identical on every host loop pass, and
            // re-joined mid-pattern when play starts mid-window.
            const juce::int64 i = window > 0 ? (juce::int64) wrapIndex (k, window) : k;

            const int nHeld = (int) seq.size();
            int raw = seq.front();
            switch (p.mode)
            {
                case ModuleOptions::kModeDown:
                    raw = seq[(size_t) (nHeld - 1 - wrapIndex (i, nHeld))];
                    break;
                case ModuleOptions::kModeUpDown:
                {
                    // Classic up-down: endpoints aren't doubled, so the cycle
                    // is 2n-2 steps (n > 1).
                    const int cycle = nHeld > 1 ? 2 * nHeld - 2 : 1;
                    const int j = wrapIndex (i, cycle);
                    raw = seq[(size_t) (j < nHeld ? j : 2 * (nHeld - 1) - j)];
                    break;
                }
                case ModuleOptions::kModeRandom:
                    raw = seq[(size_t) rng.nextInt (nHeld)];
                    break;
                default:   // kModeUp
                    raw = seq[(size_t) wrapIndex (i, nHeld)];
                    break;
            }
            out.addEvent (juce::MidiMessage::noteOn (kGenChannel, raw, (juce::uint8) kGenVelocity), s);
            st.activeGen.push_back ({ raw, kGenChannel, s + gate });
        });
    }

    releaseDueGates (st.activeGen, out, ctx.numSamples);
}

void Engine::processRhythmize (const GraphNode& node, NodeState& st,
                               const juce::MidiBuffer& in, juce::MidiBuffer& out,
                               const BlockContext& ctx)
{
    if (ctx.justStopped)
        flushActive (st.activeGen, out, 0);

    // The Arp's input contract: input notes are the module's data, consumed
    // while playing and passed through while stopped so live playing stays
    // audible. Unlike the Arp the held velocities are kept (heldVel), so a
    // retriggered chord preserves the played dynamics.
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int  s = meta.samplePosition;
        if (m.isNoteOn())
        {
            st.heldVel[(size_t) m.getNoteNumber()] = m.getVelocity();
            if (! ctx.isPlaying)
            {
                out.addEvent (m, s);
                st.passedOn.push_back ({ m.getChannel(), m.getNoteNumber() });
            }
        }
        else if (m.isNoteOff())
        {
            st.heldVel[(size_t) m.getNoteNumber()] = 0;
            if (eraseKey (st.passedOn, m.getChannel(), m.getNoteNumber()))
                out.addEvent (m, s);
        }
        else
        {
            out.addEvent (m, s);
        }
    }

    if (ctx.isPlaying)
    {
        const auto& p = node.params;
        runSteps (ctx, p.stepQn, p.gateFrac, [&] (juce::int64 k, int s, int gate)
        {
            // The pattern position is the grid index modded into the 16 steps —
            // a pure function of song position, so the pattern sits identically
            // on every host loop pass and play can start mid-pattern.
            if ((p.rhythmMask & (1u << wrapIndex (k, ModuleOptions::kRhythmSteps))) == 0)
                return;
            for (int nn = 0; nn < 128; ++nn)
                if (st.heldVel[(size_t) nn] != 0)
                {
                    out.addEvent (juce::MidiMessage::noteOn (kGenChannel, nn,
                                                             st.heldVel[(size_t) nn]), s);
                    st.activeGen.push_back ({ nn, kGenChannel, s + gate });
                }
        });
    }

    releaseDueGates (st.activeGen, out, ctx.numSamples);
}

void Engine::processHarmonizer (const GraphNode& node, NodeState& st,
                                const juce::MidiBuffer& in, juce::MidiBuffer& out,
                                const BlockContext& ctx)
{
    const auto& p = node.params;

    // The extra voices to stack above a played note (excluding the note
    // itself, which passes through as the bass). Diatonic scale-degree
    // stacking in the module's Root/Scale, or fixed chromatic intervals with
    // the scale Off; the octaver types add an octave (+ fifth). Voices
    // strictly above the played note, inversion re-voicing the harmony only,
    // deduplicated so a collapsed voice can't be double-booked.
    auto voicesFor = [&] (int played)
    {
        std::vector<int> voices;
        std::vector<int> tones;   // full stack; tones[0] is the played-note slot
        if (p.scale == ModuleOptions::kScaleOff)
        {
            for (int semi : ModuleOptions::harmonizerChromaticIntervals (p.harmType))
                tones.push_back (juce::jlimit (0, 127, played + semi));
        }
        else
        {
            const int hRoot  = p.root  >= 0 ? p.root  : ctx.root;
            const int hScale = p.scale >= 0 ? p.scale : ctx.scaleIndex;
            if (p.harmType <= ModuleOptions::kHarm6th)
            {
                for (int off : ModuleOptions::chordTypeDegrees (p.harmType))
                    tones.push_back (ScaleTables::stepInScale (played, hRoot, hScale, off));
            }
            else if (p.harmType == ModuleOptions::kHarmOctave)
            {
                tones.push_back (played);
                tones.push_back (juce::jlimit (0, 127, played + 12));
            }
            else   // kHarmOctaveFifth
            {
                tones.push_back (played);
                tones.push_back (ScaleTables::snapToScale (juce::jmin (127, played + 7), hRoot, hScale));
                tones.push_back (juce::jlimit (0, 127, played + 12));
            }
        }

        for (size_t i = 1; i < tones.size(); ++i)
            if (tones[i] > played)
                voices.push_back (tones[i]);
        std::sort (voices.begin(), voices.end());
        // Inversion lifts the lowest `inv` voices an octave — the played note
        // stays the bass, so this re-voices the harmony without moving the root.
        const int inv = juce::jlimit (0, (int) voices.size(), p.harmInversion);
        for (int i = 0; i < inv; ++i)
            voices[(size_t) i] = juce::jmin (127, voices[(size_t) i] + 12);
        std::sort (voices.begin(), voices.end());
        voices.erase (std::unique (voices.begin(), voices.end()), voices.end());
        return voices;
    };

    // Emit one played note: its own pitch as the bass plus (when asked) the
    // stacked voices, all sharing its channel/velocity and booked against the
    // played key so its note-off releases the whole stack.
    auto emitNote = [&] (int nn, int ch, int vel, int s, bool withVoices)
    {
        out.addEvent (juce::MidiMessage::noteOn (ch, nn, (juce::uint8) vel), s);
        st.sounding.push_back ({ ch, nn, ch, nn, false });
        if (withVoices)
            for (int v : voicesFor (nn))
            {
                out.addEvent (juce::MidiMessage::noteOn (ch, v, (juce::uint8) vel), s);
                st.sounding.push_back ({ ch, nn, ch, v, true });
            }
    };

    // Release just the voices booked for one played note (its bass stays
    // sounding) — Top mode uses this when a note stops being the top.
    auto releaseVoices = [&] (int inNote, int inChannel, int s)
    {
        for (auto it = st.sounding.begin(); it != st.sounding.end();)
        {
            if (it->harmVoice && it->inNote == inNote && it->inChannel == inChannel)
            {
                out.addEvent (juce::MidiMessage::noteOff (it->outChannel, it->outNote), s);
                it = st.sounding.erase (it);
            }
            else
                ++it;
        }
    };

    // Release everything sounding on a channel (bass + voices) — Replace mode
    // is monophonic, so a new note cuts the previous note and its whole stack.
    auto releaseChannel = [&] (int inChannel, int s)
    {
        for (auto it = st.sounding.begin(); it != st.sounding.end();)
        {
            if (it->inChannel == inChannel)
            {
                out.addEvent (juce::MidiMessage::noteOff (it->outChannel, it->outNote), s);
                it = st.sounding.erase (it);
            }
            else
                ++it;
        }
    };

    // Highest held note on a channel (-1 = none) and its struck velocity — Top
    // mode's top-finding and re-promotion.
    auto topHeldNote = [&] (int ch)
    {
        int top = -1;
        for (const auto& h : st.harmHeld)
            if (h.channel == ch)
                top = juce::jmax (top, h.note);
        return top;
    };
    auto heldVelocity = [&] (int ch, int nn)
    {
        for (const auto& h : st.harmHeld)
            if (h.channel == ch && h.note == nn)
                return h.velocity;
        return kGenVelocity;
    };

    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int  s = meta.samplePosition;

        if (m.isNoteOn())
        {
            const int nn  = m.getNoteNumber();
            const int ch  = m.getChannel();
            const int vel = m.getVelocity();
            switch (p.harmMode)
            {
                case ModuleOptions::kHarmReplace:
                    // Monophonic: cut the previous note and its stack, then
                    // harmonise the new note alone.
                    releaseChannel (ch, s);
                    emitNote (nn, ch, vel, s, true);
                    break;
                case ModuleOptions::kHarmTop:
                {
                    // Harmonise only the highest held note. A new note above
                    // the current top takes the harmony (the old top loses its
                    // voices, keeps its bass); a lower note passes through dry.
                    const int oldTop = topHeldNote (ch);   // before nn joins
                    if (oldTop < 0 || nn > oldTop)
                    {
                        if (oldTop >= 0)
                            releaseVoices (oldTop, ch, s);
                        emitNote (nn, ch, vel, s, true);
                    }
                    else
                        emitNote (nn, ch, vel, s, false);
                    break;
                }
                default:   // kHarmAdd
                    emitNote (nn, ch, vel, s, true);
                    break;
            }
            st.harmHeld.push_back ({ ch, nn, vel });
        }
        else if (m.isNoteOff())
        {
            const int nn = m.getNoteNumber();
            const int ch = m.getChannel();

            // Release everything booked for this key (bass + voices). A key
            // with no bookings (Replace cut it earlier) releases nothing.
            for (auto it = st.sounding.begin(); it != st.sounding.end();)
            {
                if (it->inNote == nn && it->inChannel == ch)
                {
                    out.addEvent (juce::MidiMessage::noteOff (it->outChannel, it->outNote), s);
                    it = st.sounding.erase (it);
                }
                else
                    ++it;
            }

            for (auto it = st.harmHeld.begin(); it != st.harmHeld.end(); ++it)
                if (it->channel == ch && it->note == nn)
                {
                    st.harmHeld.erase (it);
                    break;
                }

            // Top mode: if the released note was the top, promote the new
            // highest held note — give it the harmony the old top gave up.
            if (p.harmMode == ModuleOptions::kHarmTop)
            {
                const int newTop = topHeldNote (ch);
                if (newTop >= 0 && nn > newTop)
                    for (int v : voicesFor (newTop))
                    {
                        out.addEvent (juce::MidiMessage::noteOn (ch, v,
                                          (juce::uint8) heldVelocity (ch, newTop)), s);
                        st.sounding.push_back ({ ch, newTop, ch, v, true });
                    }
            }
        }
        else
        {
            out.addEvent (m, s);
        }
    }
}

// --- Pitch mappers -----------------------------------------------------------

void Engine::processScaleMod (const GraphNode& node, NodeState& st,
                              const juce::MidiBuffer& in, juce::MidiBuffer& out,
                              const BlockContext& ctx)
{
    const auto& p = node.params;
    // Scale Off means "don't force onto a scale": an identity map, but still
    // bookkept so on/off pairing survives a mid-note settings change.
    const bool snap  = p.scale != ModuleOptions::kScaleOff;
    const int  mRoot = p.root  >= 0 ? p.root  : ctx.root;
    const int  mScale = p.scale >= 0 ? p.scale : ctx.scaleIndex;
    mapNoteStream (in, out, st.sounding, [&] (int note, int)
    {
        return snap ? ScaleTables::snapToScale (note, mRoot, mScale) : note;
    });
}

void Engine::processProgression (const GraphNode& node, NodeState& st,
                                 const juce::MidiBuffer& in, juce::MidiBuffer& out,
                                 const BlockContext& ctx)
{
    const auto& p = node.params;

    // Which progression step applies at block sample `s`. Stopped transport
    // pins the progression to its first step, so auditioning matches how
    // playback will start.
    auto stepAt = [&] (int s) -> int
    {
        if (! ctx.isPlaying || p.progStepCount <= 0 || p.progRateQn <= 0.0)
            return 0;
        const double qn = ctx.blockStartQn + (double) s / ctx.samplesPerQn;
        return wrapIndex ((juce::int64) std::floor (qn / p.progRateQn), p.progStepCount);
    };

    mapNoteStream (in, out, st.sounding, [&] (int note, int s)
    {
        if (p.progStepCount <= 0)
            return note;
        const int i = stepAt (s);
        const int degree = p.progDegrees[(size_t) i];
        const int octave = p.progOctaves[(size_t) i];
        int q = note;
        // Degree I / octave 0 is a strict no-op (like an idle Shift): the
        // degree walk would otherwise snap out-of-scale notes to the scale.
        if (degree != 0 || octave != 0)
        {
            if (degree != 0)
            {
                // Scale Off walks degrees chromatically (Chromatic's 12 members).
                const int pScale = p.scale == ModuleOptions::kScaleOff
                                       ? ModuleOptions::kChromaticScale
                                       : p.scale >= 0 ? p.scale : ctx.scaleIndex;
                q = ScaleTables::stepInScale (q,
                                              p.root >= 0 ? p.root : ctx.root,
                                              pScale, degree);
            }
            q = juce::jlimit (0, 127, q + 12 * octave);
        }
        return q;
    });
}

void Engine::processShift (const GraphNode& node, NodeState& st,
                           const juce::MidiBuffer& in, juce::MidiBuffer& out,
                           const BlockContext& ctx)
{
    const auto& p = node.params;
    mapNoteStream (in, out, st.sounding, [&] (int note, int)
    {
        // Amount 0 is a strict no-op: degree-shifting snaps out-of-scale notes
        // to the scale as part of the walk, but an idle Shift must not quantize.
        if (p.shiftAmount == 0)
            return note;
        if (p.scale == ModuleOptions::kScaleOff)
            return juce::jlimit (0, 127, note + p.shiftAmount);
        return ScaleTables::stepInScale (note,
                                         p.root  >= 0 ? p.root  : ctx.root,
                                         p.scale >= 0 ? p.scale : ctx.scaleIndex,
                                         p.shiftAmount);
    });
}

void Engine::processMirror (const GraphNode& node, NodeState& st,
                            const juce::MidiBuffer& in, juce::MidiBuffer& out,
                            const BlockContext& ctx)
{
    const auto& p = node.params;
    const bool chromatic = p.scale == ModuleOptions::kScaleOff;
    const int  mRoot  = p.root  >= 0 ? p.root  : ctx.root;
    const int  mScale = p.scale >= 0 ? p.scale : ctx.scaleIndex;

    // Snap the window edges into the scale in diatonic mode so the whole
    // module stays in key — a clamped straggler then also lands in-scale.
    int lo = juce::jlimit (0, 127, p.mirrorLow);
    int hi = juce::jlimit (0, 127, p.mirrorHigh);
    if (! chromatic)
    {
        lo = ScaleTables::snapToScale (lo, mRoot, mScale);
        hi = ScaleTables::snapToScale (hi, mRoot, mScale);
    }
    if (lo > hi)
        std::swap (lo, hi);

    mapNoteStream (in, out, st.sounding, [&] (int note, int)
    {
        int q = note;
        // 1. Invert around the centre (Off = skip; the note falls straight
        //    through to the window stage).
        if (p.mirrorCenter >= 0)
            q = reflectAround (q, p.mirrorCenter, mRoot, mScale, chromatic);

        // 2. Keep it inside [lo, hi]. Limit drops an out-of-window note;
        //    Mirror folds it once across the nearest edge, clamping if a
        //    single fold still overshoots (a note more than a window-width out).
        if (q < lo || q > hi)
        {
            if (p.mirrorBounds == ModuleOptions::kMirrorLimit)
                return -1;   // dropped: nothing emitted, no off booked
            q = reflectAround (q, q < lo ? lo : hi, mRoot, mScale, chromatic);
            q = juce::jlimit (lo, hi, q);
        }
        return q;
    });
}

// --- Time modulators ---------------------------------------------------------

void Engine::processQuantize (const GraphNode& node, NodeState& st,
                              const juce::MidiBuffer& in, juce::MidiBuffer& out,
                              const BlockContext& ctx)
{
    const auto& p = node.params;

    if (ctx.justStopped)
    {
        // The deferred queue is buffered material: discarded on stop (the
        // shared transport rule). Notes already fired but converted to a gate
        // release now, like every gate list.
        st.pendingQuant.clear();
        flushActive (st.activeGen, out, 0);
    }

    if (! ctx.isPlaying)
    {
        // No grid to quantize to: pass everything through, but keep the
        // sounding bookkeeping so offs pair with ons across a transport start.
        for (const auto meta : in)
        {
            const auto m = meta.getMessage();
            const int  s = meta.samplePosition;
            if (m.isNoteOn())
            {
                out.addEvent (m, s);
                st.quantSounding.push_back ({ m.getChannel(), m.getNoteNumber() });
            }
            else if (m.isNoteOff())
            {
                // Swallow an off whose on was discarded (stop) or never fired.
                if (eraseKey (st.quantSounding, m.getChannel(), m.getNoteNumber()))
                    out.addEvent (m, s);
            }
            else
                out.addEvent (m, s);
        }
        releaseDueGates (st.activeGen, out, ctx.numSamples);
        return;
    }

    const double stepQn  = juce::jmax (0.001, p.stepQn);
    const double swingQn = juce::jlimit (0.0, 1.0, p.swing) * 0.5 * stepQn;

    // The next swung grid point at or after block sample `s`, as a
    // block-relative sample (possibly past this block's end). Boundary index j
    // counts from the song's bar 0; swing pushes odd boundaries late by
    // swing/2 of a step (pair-based model — even boundaries, the pair starts,
    // stay put), so the parity — and with it the shuffle — is fixed to the
    // song's bars, not to when play was pressed. The scan starts one boundary
    // back because a swung odd point can still be ahead of `s` when its
    // straight position has already passed.
    auto quantTarget = [&] (int s) -> int
    {
        // Half a sample of tolerance: `s` is itself rounded to a whole sample,
        // so a note sitting exactly on a boundary can read as a hair past it —
        // without the allowance it would be deferred a full step.
        const double atQn = ctx.blockStartQn + ((double) s - 0.5) / ctx.samplesPerQn;
        for (auto j = (juce::int64) std::floor (atQn / stepQn) - 1; ; ++j)
        {
            const double swungQn = swungBoundaryQn (j, stepQn, swingQn);
            if (swungQn >= atQn)
                return juce::jmax (s, (int) std::llround ((swungQn - ctx.blockStartQn)
                                                              * ctx.samplesPerQn));
        }
    };

    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int  s = meta.samplePosition;
        if (m.isNoteOn())
        {
            st.pendingQuant.push_back ({ m.getNoteNumber(), m.getChannel(),
                                         (int) m.getVelocity(), quantTarget (s), s, -1 });
        }
        else if (m.isNoteOff())
        {
            // Released while its on is still waiting: keep the played duration
            // — the note now releases itself that long after it finally sounds.
            bool converted = false;
            for (auto& q : st.pendingQuant)
                if (q.gateSamples < 0 && q.note == m.getNoteNumber()
                    && q.channel == m.getChannel())
                {
                    q.gateSamples = juce::jmax (1, s - q.arrivalOffset);
                    converted = true;
                    break;
                }
            if (! converted)
            {
                if (eraseKey (st.quantSounding, m.getChannel(), m.getNoteNumber()))
                    out.addEvent (m, s);
                // else: the on was discarded on a stop — swallow.
            }
        }
        else
        {
            out.addEvent (m, s);
        }
    }

    // Sound the deferred notes whose grid point lands this block (including
    // ones booked just above, when the target sits within the block).
    for (auto& q : st.pendingQuant)
    {
        if (q.samplesUntil >= ctx.numSamples)
            continue;
        const int at = juce::jmax (0, q.samplesUntil);
        out.addEvent (juce::MidiMessage::noteOn (q.channel, q.note, (juce::uint8) q.velocity), at);
        if (q.gateSamples >= 0)
            st.activeGen.push_back ({ q.note, q.channel, q.samplesUntil + q.gateSamples });
        else
            st.quantSounding.push_back ({ q.channel, q.note });
        q.velocity = 0;   // mark as fired for the sweep below
    }
    st.pendingQuant.erase (std::remove_if (st.pendingQuant.begin(), st.pendingQuant.end(),
                                           [] (const QuantNote& q) { return q.velocity == 0; }),
                           st.pendingQuant.end());
    for (auto& q : st.pendingQuant)
    {
        q.samplesUntil  -= ctx.numSamples;
        q.arrivalOffset -= ctx.numSamples;
    }

    releaseDueGates (st.activeGen, out, ctx.numSamples);
}

void Engine::processDelay (const GraphNode& node, NodeState& st,
                           const juce::MidiBuffer& in, juce::MidiBuffer& out,
                           const BlockContext& ctx)
{
    const auto& p = node.params;

    if (ctx.justStopped)
    {
        // Buffered echoes are discarded on stop; sounding ones release now.
        st.pendingEchoes.clear();
        flushActive (st.activeGen, out, 0);
    }

    const double delaySamples = juce::jmax (1.0, ctx.samplesPerQn * p.stepQn);

    // Book the next echo of a note: velocity decays by the feedback, pitch
    // moves by the per-echo shift (chromatic semitones with the scale Off,
    // scale degrees with a scale active — a zero shift is a strict no-op so an
    // un-shifted echo is never snapped onto a scale). The chain ends when the
    // velocity drops below the floor or the pitch leaves the MIDI range
    // (deliberately not clamped — repeats piling up at the range edge sound
    // worse than the run just ending).
    auto scheduleEcho = [&] (int srcNote, int channel, int srcVelocity, int atSample)
    {
        const int v = juce::roundToInt ((double) srcVelocity * p.delayFeedback);
        int nn = srcNote;
        if (p.delayShift != 0)
            nn = p.scale == ModuleOptions::kScaleOff
                     ? srcNote + p.delayShift
                     : ScaleTables::stepInScale (srcNote,
                                                 p.root  >= 0 ? p.root  : ctx.root,
                                                 p.scale >= 0 ? p.scale : ctx.scaleIndex,
                                                 p.delayShift);
        if (v < kMinEchoVelocity || nn < 0 || nn > 127)
            return;
        st.pendingEchoes.push_back ({ nn, channel, v, atSample + (int) delaySamples });
    };

    // The source stream passes through untouched; every note-on books an echo.
    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        out.addEvent (m, meta.samplePosition);
        if (m.isNoteOn())
            scheduleEcho (m.getNoteNumber(), m.getChannel(), m.getVelocity(),
                          meta.samplePosition);
    }

    // Fire the echoes due this block. Echoes run whether or not the transport
    // is playing (a live key echoes too). Index loop on purpose: a fired echo
    // appends its successor, which may itself be due within this block (short
    // delay times / big buffers) and is then reached later in the same sweep.
    const int gateSamples = juce::jmax (1, (int) (delaySamples * 0.5) - 1);
    for (size_t i = 0; i < st.pendingEchoes.size(); ++i)
    {
        const auto ec = st.pendingEchoes[i];   // by value — push_back may reallocate
        if (ec.samplesUntil >= ctx.numSamples)
            continue;
        out.addEvent (juce::MidiMessage::noteOn (ec.channel, ec.note, (juce::uint8) ec.velocity),
                      juce::jmax (0, ec.samplesUntil));
        st.activeGen.push_back ({ ec.note, ec.channel, ec.samplesUntil + gateSamples });
        scheduleEcho (ec.note, ec.channel, ec.velocity, ec.samplesUntil);
        st.pendingEchoes[i].velocity = 0;   // mark as fired for the sweep below
    }
    st.pendingEchoes.erase (std::remove_if (st.pendingEchoes.begin(), st.pendingEchoes.end(),
                                            [] (const EchoNote& e) { return e.velocity == 0; }),
                            st.pendingEchoes.end());
    for (auto& e : st.pendingEchoes)
        e.samplesUntil -= ctx.numSamples;

    releaseDueGates (st.activeGen, out, ctx.numSamples);
}

void Engine::processStrum (const GraphNode& node, NodeState& st,
                           const juce::MidiBuffer& in, juce::MidiBuffer& out,
                           const BlockContext& ctx)
{
    const auto& p = node.params;

    if (ctx.justStopped)
    {
        // Shared transport rule: buffered material is discarded. Buffered
        // note-ons never sounded, so they are dropped — and their upstream
        // note-offs, which will still arrive (the source flushes its gates on
        // the same stop), must then be swallowed on arrival (swallowOffs). A
        // buffered note-off releases a note that IS sounding, so it fires now
        // — unless its own on was just dropped, in which case both vanish.
        std::vector<KeyRef> droppedOns;
        for (const auto& e : st.pendingStrum)
            if (e.msg.isNoteOn())
                droppedOns.push_back ({ e.msg.getChannel(), e.msg.getNoteNumber() });
        for (const auto& e : st.pendingStrum)
            if (e.msg.isNoteOff())
            {
                if (! eraseKey (droppedOns, e.msg.getChannel(), e.msg.getNoteNumber()))
                    out.addEvent (e.msg, 0);
            }
        // An open group's withheld notes never sounded either; a grouped note
        // whose off already arrived (held in the group) vanishes with it.
        for (const auto& gn : st.strumGroup.notes)
        {
            bool offArrived = false;
            for (const auto& o : st.strumGroup.offs)
                if (o.msg.getChannel() == gn.channel && o.msg.getNoteNumber() == gn.note)
                {
                    offArrived = true;
                    break;
                }
            if (! offArrived)
                droppedOns.push_back ({ gn.channel, gn.note });
        }
        for (const auto& k : droppedOns)
            st.swallowOffs.push_back (k);
        st.pendingStrum.clear();
        st.strumGroup = {};
        // Sounding strummed notes stay: their upstream offs arrive this block
        // (or later, for live keys) and pass through undelayed, releasing them.
        st.strumHeld.clear();
    }

    // The spread gap is per consecutive note (tempo-synced): a 3-note chord at
    // a 1/16 gap lands at 0, +1/16, +2/16 — the whole fan spans (n-1) gaps.
    const double gapSamples = juce::jmax (0.0, p.strumGapQn) * ctx.samplesPerQn;
    // The fan only needs a detection window when it actually reshapes the
    // chord. With spread and both reshapers off, notes pass straight through
    // (window 0 = each note is its own one-note group, zero added latency).
    const bool fanActive = gapSamples > 0.0 || p.strumVelTilt != 0.0
                                            || p.strumJitter != 0.0;
    const int  groupWindow = fanActive
                                 ? juce::jmax (1, (int) std::llround (kStrumGroupWindowSec * sr))
                                 : 0;

    auto scheduleStrum = [&] (const juce::MidiMessage& msg, int at)
    {
        if (at < ctx.numSamples)
            out.addEvent (msg, juce::jmax (0, at));
        else
            st.pendingStrum.push_back ({ msg, at });
    };

    // A song-position tick index for the deterministic jitter hash, so a
    // looped part strums identically on every pass.
    auto strumGridIndex = [&] (int baseSample)
    {
        return (juce::int64) std::llround ((ctx.blockStartQn + (double) baseSample / ctx.samplesPerQn)
                                               * 960.0);
    };

    // Fan a finished chord out from `baseSample`: sort by pitch, order per
    // Direction, then emit each note delayed by its curve position (+jitter)
    // across the (n-1)-gap fan span, ramping velocity across the fan. Records each note as
    // sounding (for its off's matching delay and for Repeat), and re-times any
    // off that arrived before the chord finished.
    auto finalizeStrum = [&] (std::vector<StrumInNote>& notes,
                              std::vector<StrumInOff>& offs,
                              int baseSample, juce::int64 si)
    {
        const int nNotes = (int) notes.size();
        if (nNotes == 0)
            return;
        std::sort (notes.begin(), notes.end(),
                   [] (const StrumInNote& a, const StrumInNote& b) { return a.note < b.note; });

        std::vector<int> order ((size_t) nNotes);      // strum position k -> pitch index
        for (int i = 0; i < nNotes; ++i)
            order[(size_t) i] = i;                     // Up = ascending
        bool descending = false;
        switch (p.mode)
        {
            case ModuleOptions::kModeDown:   descending = true; break;
            case ModuleOptions::kModeUpDown: descending = (si & 1) != 0; break;   // alternate per strum
            case ModuleOptions::kModeRandom:
                for (int i = nNotes - 1; i > 0; --i)
                    std::swap (order[(size_t) i], order[(size_t) rng.nextInt (i + 1)]);
                break;
            default: break;   // kModeUp
        }
        if (descending)
            std::reverse (order.begin(), order.end());

        const juce::int64 gi = strumGridIndex (baseSample);
        for (int k = 0; k < nNotes; ++k)
        {
            auto& note = notes[(size_t) order[(size_t) k]];
            const double pos = nNotes > 1 ? (double) k / (double) (nNotes - 1) : 0.0;

            double f = pos;   // cumulative offset fraction, shaped by the curve
            if (p.strumCurve == ModuleOptions::kStrumCurveAccelerate)
                f = 1.0 - (1.0 - pos) * (1.0 - pos);   // concave: bunch toward the end
            else if (p.strumCurve == ModuleOptions::kStrumCurveDecelerate)
                f = pos * pos;                          // convex: bunch toward the start

            const int key = note.note * 16 + note.channel;
            const double jitterSamples = hash01 (gi, key, kStrumSaltTime)
                                             * p.strumJitter * kStrumJitterTimeFrac * gapSamples;
            // The curve fraction f scales the whole fan span: (n-1) gaps.
            const int emit = baseSample
                             + (int) std::llround (f * (double) (nNotes - 1) * gapSamples
                                                   + jitterSamples);

            const double ramp   = 2.0 * pos - 1.0;   // first struck note -1, last +1
            const double velJit = (hash01 (gi, key, kStrumSaltVel) * 2.0 - 1.0)
                                      * p.strumJitter * kStrumJitterVelRange;
            const int vel = juce::jlimit (1, 127,
                (int) std::llround ((double) note.velocity
                                        * (1.0 + p.strumVelTilt * ramp * kStrumVelTiltDepth)
                                    + velJit));

            const int delay = emit - note.arrival;   // total shift, reused for the off
            scheduleStrum (juce::MidiMessage::noteOn (note.channel, note.note, (juce::uint8) vel), emit);

            st.strumHeld.erase (std::remove_if (st.strumHeld.begin(), st.strumHeld.end(),
                [&] (const StrumHeld& h) { return h.channel == note.channel && h.note == note.note; }),
                st.strumHeld.end());
            st.strumHeld.push_back ({ note.channel, note.note, note.velocity, delay });

            for (auto& o : offs)
                if (o.msg.getChannel() == note.channel && o.msg.getNoteNumber() == note.note)
                    scheduleStrum (o.msg, o.arrival + delay);
        }
        notes.clear();
        offs.clear();
    };

    auto finalizeOpenGroup = [&] (int baseSample)
    {
        finalizeStrum (st.strumGroup.notes, st.strumGroup.offs, baseSample, st.strumIndex++);
        st.strumGroup.open = false;
    };

    // 1) Fire buffered events (fanned ons + delayed offs) due this block.
    for (auto& e : st.pendingStrum)
        if (e.samplesUntil < ctx.numSamples)
        {
            out.addEvent (e.msg, juce::jmax (0, e.samplesUntil));
            e.samplesUntil = std::numeric_limits<int>::min();   // mark fired
        }
    st.pendingStrum.erase (std::remove_if (st.pendingStrum.begin(), st.pendingStrum.end(),
                                           [] (const StrumEvent& e)
                                           { return e.samplesUntil == std::numeric_limits<int>::min(); }),
                           st.pendingStrum.end());

    // 2) Repeat: re-strum the currently-sounding chord on the bar grid. Needs
    //    the transport, so it only fires while playing. Each boundary releases
    //    the sounding instance and re-strikes it as a fresh strum (a new
    //    strumIndex, so Up-Down alternates its stroke across repeats).
    if (ctx.isPlaying && p.repeatQn > 0.0 && ! st.strumHeld.empty())
    {
        const double repQn = p.repeatQn;
        for (auto k = (juce::int64) std::ceil (ctx.blockStartQn / repQn); ; ++k)
        {
            const double qn = (double) k * repQn;
            if (qn >= ctx.blockEndQn)
                break;
            const int s = juce::jlimit (0, ctx.numSamples - 1,
                                        (int) std::llround ((qn - ctx.blockStartQn) * ctx.samplesPerQn));
            if (st.strumHeld.empty())
                break;
            std::vector<StrumInNote> chord;
            for (const auto& h : st.strumHeld)
            {
                chord.push_back ({ h.note, h.channel, h.velocity, s });
                out.addEvent (juce::MidiMessage::noteOff (h.channel, h.note), s);   // release the old instance
            }
            st.strumHeld.clear();
            std::vector<StrumInOff> none;
            finalizeStrum (chord, none, s, st.strumIndex++);
        }
    }

    // 3) Group this block's fresh note events and fan the finished chords.
    for (const auto meta : in)
    {
        const auto msg = meta.getMessage();
        const int  s   = meta.samplePosition;

        // A chord is finished once this event sits at or past the group's
        // detection deadline; fan it from the deadline (the earliest the full
        // chord is known) before handling the event itself.
        if (st.strumGroup.open && s >= st.strumGroup.deadline)
            finalizeOpenGroup (st.strumGroup.deadline);

        if (msg.isNoteOn())
        {
            // A fresh on for a key whose swallowed off never came means the
            // source reused the key; forget the stale swallow so the new
            // note's off passes.
            eraseKey (st.swallowOffs, msg.getChannel(), msg.getNoteNumber());
            if (groupWindow <= 0)
            {
                // Bypass path: pass the note straight through, but still book
                // it as sounding so its off tracks and Repeat can find it.
                std::vector<StrumInNote> one { { msg.getNoteNumber(), msg.getChannel(),
                                                 (int) msg.getVelocity(), s } };
                std::vector<StrumInOff> none;
                finalizeStrum (one, none, s, st.strumIndex++);
            }
            else
            {
                if (! st.strumGroup.open)
                {
                    st.strumGroup = {};
                    st.strumGroup.open = true;
                    st.strumGroup.deadline = s + groupWindow;
                }
                st.strumGroup.notes.push_back ({ msg.getNoteNumber(), msg.getChannel(),
                                                 (int) msg.getVelocity(), s });
            }
        }
        else if (msg.isNoteOff())
        {
            if (eraseKey (st.swallowOffs, msg.getChannel(), msg.getNoteNumber()))
                continue;   // releasing a note whose on was discarded on a stop

            bool inGroup = false;
            if (st.strumGroup.open)
                for (const auto& gnv : st.strumGroup.notes)
                    if (gnv.channel == msg.getChannel() && gnv.note == msg.getNoteNumber())
                    {
                        inGroup = true;
                        break;
                    }
            if (inGroup)
                st.strumGroup.offs.push_back ({ msg, s });   // released before the chord finished
            else
            {
                // Release a sounding strummed note delayed by the same amount
                // its on was; an unknown note (never strummed) passes straight.
                int delay = 0;
                for (auto it = st.strumHeld.begin(); it != st.strumHeld.end(); ++it)
                    if (it->channel == msg.getChannel() && it->note == msg.getNoteNumber())
                    {
                        delay = it->delay;
                        st.strumHeld.erase (it);
                        break;
                    }
                scheduleStrum (msg, s + delay);
            }
        }
        else
        {
            out.addEvent (msg, s);   // CC / pitch-bend / clock: left in place
        }
    }

    // A group whose deadline lands within this block but that no later event
    // closed finalizes now; one still open past the block carries over (its
    // samples aged like every other pending buffer).
    if (st.strumGroup.open)
    {
        if (st.strumGroup.deadline < ctx.numSamples)
            finalizeOpenGroup (st.strumGroup.deadline);
        else
        {
            st.strumGroup.deadline -= ctx.numSamples;
            for (auto& gnv : st.strumGroup.notes) gnv.arrival -= ctx.numSamples;
            for (auto& o   : st.strumGroup.offs)  o.arrival   -= ctx.numSamples;
        }
    }

    // Age the still-buffered events into the next block.
    for (auto& e : st.pendingStrum)
        e.samplesUntil -= ctx.numSamples;
}

void Engine::processHumanize (const GraphNode& node, NodeState& st,
                              const juce::MidiBuffer& in, juce::MidiBuffer& out,
                              const BlockContext& ctx)
{
    const auto& p = node.params;

    if (ctx.justStopped)
    {
        // Same contract as Strum's stop path: buffered ons never sounded, so
        // they are dropped and their eventual input offs swallowed; a buffered
        // off releases a sounding note and fires now — unless its own on was
        // just dropped, in which case both vanish.
        std::vector<KeyRef> droppedOns;
        for (const auto& e : st.pendingHuman)
            if (e.msg.isNoteOn())
                droppedOns.push_back ({ e.msg.getChannel(), e.msg.getNoteNumber() });
        for (const auto& e : st.pendingHuman)
            if (! e.msg.isNoteOn())
            {
                if (! (e.msg.isNoteOff()
                       && eraseKey (droppedOns, e.msg.getChannel(), e.msg.getNoteNumber())))
                    out.addEvent (e.msg, 0);
            }
        for (const auto& k : droppedOns)
            st.swallowOffs.push_back (k);
        st.pendingHuman.clear();
        st.humanHeld.clear();
    }

    if (! ctx.isPlaying)
    {
        // Stopped: no grid to groove against — events pass straight through
        // for immediate live feel (minus the offs owed to dropped ons).
        for (const auto meta : in)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
                eraseKey (st.swallowOffs, m.getChannel(), m.getNoteNumber());
            else if (m.isNoteOff()
                     && eraseKey (st.swallowOffs, m.getChannel(), m.getNoteNumber()))
                continue;
            out.addEvent (m, meta.samplePosition);
        }
        return;
    }

    const double stepQn  = juce::jmax (0.001, p.stepQn);
    const double swingQn = p.swing * 0.5 * stepQn;
    const double layQn   = p.humanizeLayback * kHumanizeLaybackFrac * stepQn;

    auto emitOrDefer = [&] (const juce::MidiMessage& msg, int newSample)
    {
        if (newSample < ctx.numSamples)
            out.addEvent (msg, juce::jmax (0, newSample));
        else
            st.pendingHuman.push_back ({ msg, newSample });
    };

    // 1) Fire buffered events whose warped time lands in this block; the rest
    //    are decremented at the end (like the echo/quantize buffers).
    for (auto& e : st.pendingHuman)
        if (e.samplesUntil < ctx.numSamples)
        {
            out.addEvent (e.msg, juce::jmax (0, e.samplesUntil));
            e.samplesUntil = std::numeric_limits<int>::min();   // mark fired
        }
    st.pendingHuman.erase (std::remove_if (st.pendingHuman.begin(), st.pendingHuman.end(),
                                           [] (const HumanEvent& e)
                                           { return e.samplesUntil == std::numeric_limits<int>::min(); }),
                           st.pendingHuman.end());

    // 2) Warp this block's fresh events. The swing + lay-back offset is a pure
    //    function of song position (applied to every note event, on and off,
    //    so durations follow the groove); note-ons additionally take a random
    //    late nudge and velocity shaping, note-offs a random lengthening — all
    //    delay-only, so nothing ever moves before it arrived. A note-off
    //    reuses its on's timing jitter (kept in humanHeld) so the jitter never
    //    changes the note's length.
    for (const auto meta : in)
    {
        const auto msg = meta.getMessage();
        const int  s   = meta.samplePosition;
        const double qn       = ctx.blockStartQn + (double) s / ctx.samplesPerQn;
        const double warpedQn = swingWarpQn (qn, stepQn, swingQn) + layQn;
        const juce::int64 gi  = (juce::int64) std::floor (qn / stepQn);
        auto sampleForQn = [&] (double targetQn)
        {
            return juce::jmax (s, (int) std::llround ((targetQn - ctx.blockStartQn) * ctx.samplesPerQn));
        };

        if (msg.isNoteOn())
        {
            const int chan  = msg.getChannel();
            const int pitch = msg.getNoteNumber();
            const int key   = pitch * 16 + chan;
            eraseKey (st.swallowOffs, chan, pitch);   // key reused — forget the stale swallow

            const double jitterQn = hash01 (gi, key, kSaltTime)
                                        * p.humanizeTimeJit * kHumanizeTimeJitFrac * stepQn;

            // Accent: strong beats (even step of the swing pair) louder, weak
            // beats softer; then a symmetric random touch on top.
            const bool   strong = (gi & 1) == 0;
            const double accent = 1.0 + p.humanizeAccent * kHumanizeAccentDepth
                                            * (strong ? 1.0 : -1.0);
            const double velJit = (hash01 (gi, key, kSaltVel) * 2.0 - 1.0)
                                      * p.humanizeVelJit * kHumanizeVelRange;
            const int newVel = juce::jlimit (1, 127,
                                             (int) std::llround ((double) msg.getVelocity() * accent + velJit));

            st.humanHeld.push_back ({ chan, pitch, jitterQn });
            emitOrDefer (juce::MidiMessage::noteOn (chan, pitch, (juce::uint8) newVel),
                         sampleForQn (warpedQn + jitterQn));
        }
        else if (msg.isNoteOff())
        {
            if (eraseKey (st.swallowOffs, msg.getChannel(), msg.getNoteNumber()))
                continue;   // releasing a note whose on was discarded on a stop

            // Reuse the matching on's timing jitter so the note keeps its
            // length; add an independent one-sided lengthening on top.
            double onJitterQn = 0.0;
            for (auto it = st.humanHeld.begin(); it != st.humanHeld.end(); ++it)
                if (it->channel == msg.getChannel() && it->note == msg.getNoteNumber())
                {
                    onJitterQn = it->jitterQn;
                    st.humanHeld.erase (it);
                    break;
                }
            const double lenQn = hash01 (gi, msg.getNoteNumber() * 16 + msg.getChannel(), kSaltLen)
                                     * p.humanizeLenJit * kHumanizeLenJitFrac * stepQn;
            emitOrDefer (msg, sampleForQn (warpedQn + onJitterQn + lenQn));
        }
        else
        {
            // CC / pitch-bend / clock / sysex: left on their original sample
            // (shifting a controller or clock off its note would do more harm
            // than the groove is worth).
            out.addEvent (msg, s);
        }
    }

    // 3) Age the still-buffered events into the next block.
    for (auto& e : st.pendingHuman)
        e.samplesUntil -= ctx.numSamples;
}
