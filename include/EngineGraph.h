#pragma once

#include <array>
#include <vector>
#include "ModuleTypes.h"
#include "ModuleSettings.h"   // ModuleOptions::kMaxProgSteps

// The wired module graph as the audio thread sees it: a plain, immutable
// snapshot built on the message thread from the canvas model (modules +
// connections) and published to the engine as a whole. The engine never sees
// the canvas model itself — this is the "immutable graph snapshot" handoff the
// architecture notes predicted once real wiring replaced the presence flags.
//
// Contract:
//  - `nodes` is topologically sorted: every entry in a node's `inputs` is an
//    index of an EARLIER node. The engine runs the vector front to back, so no
//    cycles may ever be published (the processor refuses connections that
//    would create one).
//  - `inputs` lists the upstream nodes wired into this node's input port; the
//    engine merges their output streams (fan-in). Fan-out needs no
//    representation — several nodes may name the same upstream index.
//  - MIDI In nodes ignore `inputs` and read the host's incoming events;
//    Output nodes are the sinks whose output leaves the plugin.
//  - `id` is the canvas module id. The engine keys its per-node state
//    (held notes, pending buffers) on it, so state survives settings edits.
//    `topologyVersion` changes only when modules/connections are added or
//    removed; the engine responds by releasing everything and starting from
//    clean per-node state (the "patch edits cut sounding notes" rule).
//    Settings-only edits republish with the same version, keeping state.
struct ModuleParams
{
    // Shared fields (each type reads only what it uses).
    int    root    = -1;      // -1 = follow the global root
    int    scale   = -1;      // -1 = global, ModuleOptions::kScaleOff = chromatic
    double stepQn  = 0.25;    // Rate in quarter notes (also: Quantize grid,
                              // Delay time, Humanize groove grid)
    double gateFrac = 0.5;    // note length as a fraction of the step
    double repeatQn = 0.0;    // repeat window; <= 0 = Endless
    int    mode    = 0;       // ModuleOptions::kMode*
    int    octaves = 1;       // pattern span (Scale gen, Arp)
    int    channel = 0;       // I/O modules: MIDI In filter (0 = All) / Output stamp

    // Random.
    int rangeFrom = 36;
    int rangeTo   = 60;

    // Scale generator.
    bool endOnRoot = true;

    // LFO.
    double lfoCycleQn    = 4.0;
    int    lfoShape      = 0;
    int    lfoDepthOct   = 1;
    int    lfoDepthSteps = 0;
    double lfoPhase      = 0.0;

    // Chord / Drone (the Length/Repeat window pair, resolved to quarter notes;
    // period <= 0 = Endless = re-trigger back to back).
    double holdLengthQn = 4.0;
    double holdPeriodQn = 4.0;
    int    chordDegree    = 0;
    int    chordType      = 0;
    int    chordInversion = 0;
    int    droneVoicing   = 0;
    int    droneOctave    = 0;

    // Quantize / Humanize swing (0..1 pair-based fraction).
    double swing = 0.0;

    // Progression.
    double progRateQn    = 4.0;
    int    progStepCount = 0;
    std::array<int, ModuleOptions::kMaxProgSteps> progDegrees {};
    std::array<int, ModuleOptions::kMaxProgSteps> progOctaves {};

    // Shift.
    int shiftAmount = 0;

    // Mirror.
    int mirrorCenter = 60;
    int mirrorLow    = 36;
    int mirrorHigh   = 84;
    int mirrorBounds = 0;

    // Harmonizer.
    int harmType      = 0;
    int harmInversion = 0;
    int harmMode      = 0;

    // Delay.
    double delayFeedback = 0.5;
    int    delayShift    = 0;

    // Strum.
    double strumSpreadSec = 0.04;
    int    strumCurve     = 0;
    double strumVelTilt   = 0.0;
    double strumJitter    = 0.0;

    // Humanize.
    double humanizeLayback = 0.0;
    double humanizeAccent  = 0.0;
    double humanizeTimeJit = 0.0;
    double humanizeVelJit  = 0.0;
    double humanizeLenJit  = 0.0;
};

struct GraphNode
{
    int          id   = 0;
    ModuleType   type = ModuleType::MidiIn;
    ModuleParams params;
    std::vector<int> inputs;   // indices of upstream nodes (all < this node's index)
};

struct GraphSnapshot
{
    std::vector<GraphNode> nodes;   // topologically sorted
    int topologyVersion = 0;
};
