#pragma once

#include <JuceHeader.h>

/*
    FlatTileLookAndFeel
    -------------------
    • Original button system preserved
    • Solid rectangular tiles
    • Extended ONLY to support RECORD button visuals
*/

class FlatTileLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FlatTileLookAndFeel();
    ~FlatTileLookAndFeel() override = default;

    // Toggle buttons (I, L, ✓, MIDI ARM)
    void drawToggleButton (juce::Graphics&,
                           juce::ToggleButton&,
                           bool hover,
                           bool down) override;

    // Text buttons (X, TIMER / RECORD)
    void drawButtonBackground (juce::Graphics&,
                               juce::Button&,
                               const juce::Colour&,
                               bool hover,
                               bool down) override;

    void drawButtonText (juce::Graphics&,
                         juce::TextButton&,
                         bool hover,
                         bool down) override;
};
