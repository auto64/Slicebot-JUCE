#include "FlatTileLookAndFeel.h"

// -------------------------------------------------
// COLOURS (ORIGINAL)
// -------------------------------------------------

static juce::Colour darkGrey()    { return juce::Colours::darkgrey; }
static juce::Colour paleBlue()    { return juce::Colour (0xff6fa8dc); }
static juce::Colour palePurple()  { return juce::Colour (0xff9a7fd1); }
static juce::Colour midGreen()    { return juce::Colour (0xff6fbf73); }
static juce::Colour darkGreen()   { return juce::Colour (0xff2e7d32); }
static juce::Colour dangerRed()   { return juce::Colour (0xffc0392b); }

// -------------------------------------------------
// CONSTRUCTOR (ORIGINAL)
// -------------------------------------------------

FlatTileLookAndFeel::FlatTileLookAndFeel()
{
    setColour (juce::TextButton::buttonColourId,
               juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonOnColourId,
               juce::Colours::transparentBlack);
    setColour (juce::ComboBox::outlineColourId,
               juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId,
               juce::Colours::transparentBlack);
    setColour (juce::ToggleButton::textColourId,
               juce::Colours::white);
}

// -------------------------------------------------
// TOGGLE BUTTONS (UNCHANGED)
// -------------------------------------------------

void FlatTileLookAndFeel::drawToggleButton (juce::Graphics& g,
                                            juce::ToggleButton& b,
                                            bool,
                                            bool)
{
    auto r = b.getLocalBounds().toFloat();
    juce::Colour fill = darkGrey();

    if (b.getToggleState())
    {
        if (b.getButtonText() == "I")
            fill = paleBlue();
        else if (b.getButtonText() == "L")
            fill = palePurple();
        else if (b.getButtonText() == "MIDI ARM")
            fill = darkGreen();
        else
            fill = midGreen(); // ✓
    }

    g.setColour (fill);
    g.fillRect (r);

    g.setColour (juce::Colours::white);
    g.setFont (r.getHeight() * 0.66f);

    if (b.getButtonText().isEmpty())
    {
        juce::Path tick;
        tick.startNewSubPath (r.getX() + r.getWidth() * 0.25f,
                              r.getCentreY());
        tick.lineTo (r.getX() + r.getWidth() * 0.45f,
                     r.getBottom() - r.getHeight() * 0.25f);
        tick.lineTo (r.getRight() - r.getWidth() * 0.22f,
                     r.getY() + r.getHeight() * 0.28f);

        g.strokePath (tick, juce::PathStrokeType (2.0f));
    }
    else
    {
        g.drawFittedText (b.getButtonText(),
                          r.toNearestInt(),
                          juce::Justification::centred,
                          1);
    }
}

// -------------------------------------------------
// TEXT BUTTONS (X + RECORD)
// -------------------------------------------------

void FlatTileLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                                juce::Button& b,
                                                const juce::Colour&,
                                                bool,
                                                bool)
{
    auto r = b.getLocalBounds().toFloat();
    const auto name = b.getName();

    // === RECORD STATES ===
    if (name == "RECORD_IDLE" || name == "RECORD_STOPPED")
    {
        g.setColour (darkGrey());
        g.fillRect (r);

        // red dot (+25%)
        const float d = r.getHeight() * 0.45f;
        g.setColour (juce::Colours::red);
        g.fillEllipse (r.getCentreX() - d * 0.5f,
                       r.getCentreY() - d * 0.5f,
                       d, d);
        return;
    }

    if (name == "RECORD_ORANGE_ON")
    {
        g.setColour (juce::Colours::orange);
        g.fillRect (r);
        return;
    }

    if (name == "RECORD_RED_ON")
    {
        g.setColour (juce::Colours::red);
        g.fillRect (r);
        return;
    }

    // OFF frames just fall back to grey
    if (name.startsWith ("RECORD_"))
    {
        g.setColour (darkGrey());
        g.fillRect (r);
        return;
    }

    // === ORIGINAL X BUTTON ===
    g.setColour (b.getButtonText() == "X" ? dangerRed()
                                         : darkGrey());
    g.fillRect (r);
}

void FlatTileLookAndFeel::drawButtonText (juce::Graphics& g,
                                          juce::TextButton& b,
                                          bool,
                                          bool)
{
    const auto name = b.getName();

    // Hide numbers until recording starts
    if (name == "RECORD_IDLE")
        return;

    g.setColour (juce::Colours::white);
    g.setFont (b.getHeight() * 0.66f); // 66%

    g.drawFittedText (b.getButtonText(),
                      b.getLocalBounds(),
                      juce::Justification::centred,
                      1);
}
