#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <utility>

struct Preset
{
    juce::String name;
    juce::String category;
    std::vector<std::pair<const char*, float>> values;   // parameter id -> real-world value; anything unlisted = Init
};

const std::vector<Preset>& factoryPresets();
juce::StringArray presetCategories();
