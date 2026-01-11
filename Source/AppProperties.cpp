#include "AppProperties.h"

AppProperties::AppProperties()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "SliceBotJUCE";
    options.filenameSuffix      = "settings";
    options.osxLibrarySubFolder = "Application Support";

    appProperties.setStorageParameters (options);
}

AppProperties::~AppProperties() = default;

AppProperties& AppProperties::get()
{
    static AppProperties instance;
    return instance;
}

juce::ApplicationProperties& AppProperties::properties()
{
    return appProperties;
}
