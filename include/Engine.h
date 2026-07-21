#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <map>
#include <vector>
#include "EngineGraph.h"

// The MIDI engine: executes the wired module graph (GraphSnapshot) each block.
//
// Execution model, per block:
//  - The host's incoming events are set aside; the snapshot's nodes run front
//    to back (they arrive topologically sorted). Each node reads the merged
//    output streams of its wired inputs (fan-in merges; fan-out is just two
//    consumers naming the same upstream node), transforms them, and writes its
//    own output stream. MIDI In nodes read the host events instead (filtered
//    by their channel); Output nodes stamp their channel and their streams are
//    what leaves the plugin. A module wired to nothing therefore contributes
//    nothing, and with no Output node the plugin is silent — the graph is the
//    whole signal flow, there is no implicit routing.
//  - Every stream between nodes is plain MIDI (the requirements' "no custom
//    internal data format"), so any output can feed any input.
//
// Per-node state lives in the engine keyed by module id (NodeState): held
// input notes, gate countdowns, pending time-shifted events, in-flight
// note maps. Settings edits republish the snapshot with the same
// topologyVersion, so state (and sounding notes) survive them. Adding or
// removing modules/connections bumps topologyVersion; the engine then releases
// everything it has sounding at the host (hostSounding) and starts from clean
// state — the coarse but safe "patch edits cut sounding notes" rule that keeps
// the no-hanging-notes invariant trivial across rewires.
//
// The no-hanging-notes invariant is now per node: any node that emits a
// note-on remembers what it emitted (its `sounding`/gate bookkeeping) and
// emits the matching note-off itself — either when the corresponding input
// note-off arrives (mapped through the same remembered pitch/channel, so
// mid-note settings edits can't hang anything) or when its own gate expires.
// A note-off with no remembered note-on is swallowed, never forwarded, so a
// dropped or discarded note can't send a stray release downstream.
//
// Node behaviours (see modules.md for the product-level reference):
//  - MIDI In: admits host events on its channel (0 = All). Note-offs are
//    admitted iff their note-on was, so a channel edit mid-note can't hang.
//  - Generators (Random, Scale, LFO, Chord, Drone): fire on their own step
//    grids while the transport plays, emitting on channel 1 (an Output
//    restamps); gates release through per-node countdowns, and a transport
//    stop flushes everything still sounding. The Drone re-triggers mid-hold
//    when the pitches it should hold change (root/scale/voicing edits).
//  - Arp: consumes its input notes while playing (they are its input data) and
//    walks the currently held set on its grid; when stopped, input passes
//    through so live playing stays audible.
//  - Harmonizer: stacks voices on each input note-on (Add/Replace/Top over its
//    input held set); the voices ride the played note's on/off.
//  - Pitch mappers (Scale, Progression, Shift, Mirror): map each note-on's
//    pitch, remember (in -> out), release the remembered pitch on the note-off.
//    Mirror's Limit mode may drop a note entirely (nothing emitted or booked).
//  - Quantize: while playing, defers note-ons to the next point of its swung
//    grid (pair-based swing, swing-timing.md); a note released while its on is
//    still waiting keeps its duration. Stopped: pass-through. Stop discards
//    the deferred queue.
//  - Delay: passes its input through and books echo chains (velocity decay,
//    cumulative per-echo shift in semitones or degrees). Echoes run regardless
//    of transport; stop discards booked echoes and releases sounding ones.
//  - Strum: groups near-simultaneous note-ons (a fixed detection window — its
//    added latency) and fans them out over the spread; each note-off is
//    delayed like its note-on so lengths and order hold.
//  - Humanize: delay-only feel warp of its whole input stream (pair-based
//    swing as a continuous nudge, lay-back, deterministic timing/velocity/
//    length jitter hashed from song position so loops repeat).
//  - Output: stamps its channel, remembering per note the channel it stamped
//    so the note-off follows a mid-note channel edit.
//
// Timing: every grid is re-derived per block from the host ppq position (the
// LAM master-clock model, unchanged from the fixed-chain engine): a boundary
// fires when it falls inside the block's half-open [blockStart, blockEnd)
// quarter-note range; no freewheeling counters, so loop wraps and tempo
// automation cannot drift. Hosts without a usable ppq fall back to counting
// from transport start (fallbackQn); the processor synthesizes a transport for
// hosts with no playhead at all.
//
// The engine is owned and driven entirely from the audio thread. The processor
// publishes the GraphSnapshot from the message thread via a lock-free-for-the-
// audio-thread shared_ptr swap; `process` receives the current snapshot plus
// the global root/scale each block.
class Engine
{
public:
    void prepare (double sampleRate);
    void reset();

    // Transforms `midi` in place: input events are consumed by the graph's
    // MIDI In nodes and the Output nodes' streams are written back. `pos` may
    // be null (no playhead); `graph` may be null (treated as an empty graph).
    void process (juce::MidiBuffer& midi,
                  int numSamples,
                  const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos,
                  int root, int scaleIndex,
                  const GraphSnapshot* graph);

    // Per-block context shared by the node processors (public so the free
    // helper functions in Engine.cpp can take it).
    struct BlockContext
    {
        int    numSamples;
        bool   isPlaying;
        bool   justStopped;    // playing on the previous block, stopped now
        double blockStartQn;
        double blockEndQn;
        double samplesPerQn;
        int    root;
        int    scaleIndex;
    };

    // A note this node emitted and has yet to release, with the remaining
    // samples until its gate ends. `drone` marks Drone holds so the mid-hold
    // re-trigger can find them.
    struct ActiveNote { int note; int channel; int samplesLeft; bool drone = false; };

    // (incoming -> emitted) for nodes that transform notes: the input note-off
    // releases exactly what its note-on produced, whatever the settings say by
    // then. `harmVoice` marks a Harmonizer-added voice (as opposed to the
    // played note itself), so Top mode can strip voices while keeping the bass.
    struct SoundingNote { int inChannel; int inNote; int outChannel; int outNote; bool harmVoice = false; };

    struct KeyRef { int channel; int note; };

    // Quantize's deferred note-ons. samplesUntil (the grid point) and
    // arrivalOffset (when the note arrived) are block-relative and aged
    // together. gateSamples >= 0 = the off is ours to emit (the input note-off
    // already arrived, keeping the played duration); -1 = the source still
    // holds the note, so firing registers it in quantSounding and the eventual
    // input note-off releases it.
    struct QuantNote { int note; int channel; int velocity; int samplesUntil; int arrivalOffset; int gateSamples; };

    struct EchoNote { int note; int channel; int velocity; int samplesUntil; };

    struct HumanEvent { juce::MidiMessage msg; int samplesUntil; };
    struct HumanHeld  { int channel; int note; double jitterQn; };

    struct StrumInNote { int note; int channel; int velocity; int arrival; };
    struct StrumInOff  { juce::MidiMessage msg; int arrival; };
    struct StrumGroup
    {
        std::vector<StrumInNote> notes;
        std::vector<StrumInOff>  offs;
        int  deadline = 0;      // block-relative; finalize when it lands
        bool open = false;
    };
    struct StrumEvent { juce::MidiMessage msg; int samplesUntil; };
    struct StrumHeld  { int channel; int note; int velocity; int delay; };

    struct HarmHeldNote { int channel; int note; int velocity; };

    // One node's complete state. A union-of-all-types bag rather than a class
    // per type: every member is an empty vector until its node type touches
    // it, which keeps the per-type processors mechanical and the state
    // reconciliation (a map keyed by module id) trivial.
    struct NodeState
    {
        std::array<bool, 128>     held {};         // Arp: input notes down
        std::vector<KeyRef>       passedOn;        // Arp: notes passed through while stopped
        std::vector<KeyRef>       inHeld;          // MIDI In: note-ons admitted
        std::vector<ActiveNote>   activeGen;       // gate countdowns (generators, Arp,
                                                   // Delay echoes, Quantize converted notes)
        std::vector<SoundingNote> sounding;        // pitch mappers / Harmonizer / Output
        std::vector<HarmHeldNote> harmHeld;        // Harmonizer: held keys + velocities
        std::vector<QuantNote>    pendingQuant;
        std::vector<KeyRef>       quantSounding;   // Quantize: fired, source still holds
        std::vector<EchoNote>     pendingEchoes;
        std::vector<HumanEvent>   pendingHuman;
        std::vector<HumanHeld>    humanHeld;
        std::vector<KeyRef>       swallowOffs;     // Strum/Humanize: offs whose ons were
                                                   // discarded on stop — drop on arrival
        StrumGroup                strumGroup;
        std::vector<StrumEvent>   pendingStrum;
        std::vector<StrumHeld>    strumHeld;
        juce::int64               strumIndex = 0;
    };

private:
    void processNode (const GraphNode& node, NodeState& st,
                      const juce::MidiBuffer& in, juce::MidiBuffer& out,
                      const BlockContext& ctx);

    void processMidiIn     (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processOutput     (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processRandom     (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processScaleGen   (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processLfo        (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processChord      (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processDrone      (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processArp        (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processHarmonizer (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processScaleMod   (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processProgression(const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processShift      (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processMirror     (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processQuantize   (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processDelay      (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processStrum      (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);
    void processHumanize   (const GraphNode&, NodeState&, const juce::MidiBuffer&, juce::MidiBuffer&, const BlockContext&);

    double sr = 44100.0;

    // Per-node state keyed by module id (stable across settings edits; the map
    // is cleared wholesale on a topology change). std::map for stable
    // references; per-node lookups are a handful per block.
    std::map<int, NodeState> states;
    int lastTopologyVersion = -1;

    // Every (channel, note) currently sounding AT THE HOST (tracked from the
    // final outgoing buffer). The topology-change flush emits note-offs for
    // these, so a rewire can never hang a note whatever per-node state it
    // destroyed.
    std::vector<KeyRef> hostSounding;

    // Scratch output buffer per node index, reused across blocks.
    std::vector<juce::MidiBuffer> nodeBuf;

    // Clock fallback for hosts whose playhead has no ppq value: quarter notes
    // accumulated since transport start. (The processor synthesizes a full
    // position for the no-playhead cases, so this is a last-resort path.)
    double fallbackQn = 0.0;
    bool   wasPlaying = false;

    juce::Random rng;
};
