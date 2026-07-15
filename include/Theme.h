#pragma once

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Single source of truth for every paint-time colour in the editor. Two schemes
// (Light / Dark) are defined; painting code reads them fresh every frame through
// CurrentTheme::active(), so a runtime theme swap only needs to flip which scheme
// that function returns and trigger a repaint.
//
// The palette follows the SnorkelAudioStandards two-theme convention (same
// colours the other Snorkel plugins ship). It is adapted from Little Arp
// Monster's LamTheme, trimmed to what the Current canvas actually paints and
// extended with per-module-family fills (generator / modulator / I/O) that the
// node graph needs.
namespace CurrentTheme
{
    struct Scheme
    {
        // Picks JUCE's dark vs light base widget colours (combos, scrollbars)
        // for anything we don't override explicitly.
        bool         useDarkBase;

        juce::Colour windowBg;        // editor / canvas backdrop
        juce::Colour canvasBg;        // the drop surface (slightly offset from windowBg)
        juce::Colour canvasGrid;      // faint alignment grid drawn on the canvas

        juce::Colour panelBg;         // menu bar + palette tray fill
        juce::Colour panelBorder;     // panel / node outlines
        juce::Colour widgetOutline;   // combo / button outlines (via LAF)
        juce::Colour widgetBg;        // combo / button body

        juce::Colour text;            // body text — labels, combos, buttons
        juce::Colour accent;          // selection ring, popup highlight

        // Per-family node fills. Colour encodes the module family (see the
        // requirements' visual-encoding note); shape encodes the kind
        // (generators square, modulators circle). I/O uses its own tint.
        juce::Colour genFill;         // generators
        juce::Colour modFill;         // modulators
        juce::Colour ioFill;          // MIDI In / Output

        juce::Colour port;            // port dots on node edges
    };

    // Dark — navy backdrop, saturated family colours that read on it.
    inline const Scheme kDark =
    {
        true,
        juce::Colour (0xff323e44),    // windowBg
        juce::Colour (0xff283238),    // canvasBg
        juce::Colour (0xff3a464c),    // canvasGrid
        juce::Colour (0xff202036),    // panelBg
        juce::Colour (0xff404060),    // panelBorder
        juce::Colour (0xff555577),    // widgetOutline
        juce::Colour (0xff3a3a5c),    // widgetBg
        juce::Colour (0xffc8c8d8),    // text
        juce::Colour (0xff00d4ff),    // accent
        juce::Colour (0xff7ed957),    // genFill (green)
        juce::Colour (0xffb87bff),    // modFill (purple)
        juce::Colour (0xff4f9bff),    // ioFill  (blue)
        juce::Colour (0xffe6ecf5)     // port
    };

    // Light — white panels, blue accent, same family hues as Dark.
    inline const Scheme kLight =
    {
        false,
        juce::Colour (0xffeef1f6),    // windowBg
        juce::Colour (0xffe1e7f0),    // canvasBg
        juce::Colour (0xffcfd8e6),    // canvasGrid
        juce::Colour (0xffffffff),    // panelBg
        juce::Colour (0xffa8b8d4),    // panelBorder
        juce::Colour (0xff7090b8),    // widgetOutline
        juce::Colour (0xffffffff),    // widgetBg
        juce::Colour (0xff1f3a5c),    // text
        juce::Colour (0xff2070d0),    // accent
        juce::Colour (0xff7ed957),    // genFill (green)
        juce::Colour (0xffb87bff),    // modFill (purple)
        juce::Colour (0xff4f9bff),    // ioFill  (blue)
        juce::Colour (0xff1f3a5c)     // port
    };

    // Active scheme is mutable at runtime. Painting reads active() every frame;
    // setActive() + editor.applyTheme() repaints. Default Dark matches the Theme
    // parameter's default index (1).
    inline const Scheme* gActive = &kDark;
    inline const Scheme& active()                         { return *gActive; }
    inline void          setActive (const Scheme& s)      { gActive = &s;     }

    // Map the Theme-combo index (0 = Light, 1 = Dark) to a scheme. Kept in
    // lock-step with the AudioParameterChoice in PluginProcessor.
    inline const Scheme& byIndex (int i)
    {
        return (i == 1) ? kDark : kLight;
    }
}
