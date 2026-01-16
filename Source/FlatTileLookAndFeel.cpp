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
static juce::Colour lockGrey()    { return juce::Colour (0xff888888); }

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
        else if (b.getButtonText() == "REC")
            fill = darkGrey();
        else if (b.getButtonText() == "LOCK")
            fill = darkGrey();
        else
            fill = midGreen(); // ✓
    }

    g.setColour (fill);
    g.fillRect (r);

    if (b.getButtonText() == "REC")
    {
        const float d = r.getHeight() * 0.5f;
        g.setColour (juce::Colours::red.withAlpha (b.getToggleState() ? 1.0f : 0.35f));
        g.fillEllipse (r.getCentreX() - d * 0.5f,
                       r.getCentreY() - d * 0.5f,
                       d,
                       d);
        return;
    }

    if (b.getButtonText() == "LOCK")
    {
        const float w = r.getWidth() * 0.5f;
        const float h = r.getHeight() * 0.45f;
        const float x = r.getCentreX() - w * 0.5f;
        const float y = r.getCentreY() - h * 0.1f;
        g.setColour (b.getToggleState() ? juce::Colours::white : lockGrey());
        g.fillRect (x, y, w, h);
        g.drawRect (x, y, w, h, 1.0f);

        juce::Path shackle;
        shackle.addRoundedRectangle (x + w * 0.2f,
                                     y - h * 0.5f,
                                     w * 0.6f,
                                     h * 0.7f,
                                     3.0f);
        g.strokePath (shackle, juce::PathStrokeType (1.5f));
        return;
    }

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

    if (name == "RECORD_ORANGE_DIM")
    {
        g.setColour (juce::Colours::orange.withAlpha (0.4f));
        g.fillRect (r);
        return;
    }

    if (name == "RECORD_RED_ON")
    {
        g.setColour (juce::Colours::red);
        g.fillRect (r);
        return;
    }

    if (name == "RECORD_RED_DIM")
    {
        g.setColour (juce::Colours::red.withAlpha (0.4f));
        g.fillRect (r);
        return;
    }

    if (name == "PLAY_IDLE")
    {
        g.setColour (darkGrey());
        g.fillRect (r);
        g.setColour (midGreen());
        juce::Path play;
        play.startNewSubPath (r.getX() + r.getWidth() * 0.35f, r.getY() + r.getHeight() * 0.25f);
        play.lineTo (r.getX() + r.getWidth() * 0.35f, r.getBottom() - r.getHeight() * 0.25f);
        play.lineTo (r.getRight() - r.getWidth() * 0.25f, r.getCentreY());
        play.closeSubPath();
        g.fillPath (play);
        return;
    }

    if (name == "PLAYING")
    {
        g.setColour (darkGrey());
        g.fillRect (r);
        g.setColour (juce::Colours::white);
        const float size = r.getHeight() * 0.35f;
        g.fillRect (r.getCentreX() - size * 0.5f,
                    r.getCentreY() - size * 0.5f,
                    size,
                    size);
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
    if (name == "RECORD_IDLE" || name == "PLAY_IDLE" || name == "PLAYING")
        return;

    g.setColour (juce::Colours::white);
    g.setFont (b.getHeight() * 0.66f); // 66%

    g.drawFittedText (b.getButtonText(),
                      b.getLocalBounds(),
                      juce::Justification::centred,
                      1);
}
