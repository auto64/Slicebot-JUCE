#pragma once
#include <JuceHeader.h>

class AppProperties
{
public:
    static AppProperties& get();

    juce::ApplicationProperties& properties();

private:
    AppProperties();
    ~AppProperties();

    juce::ApplicationProperties appProperties;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppProperties)
};
