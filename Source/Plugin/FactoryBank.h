#pragma once
#include "Presets/PresetManager.h"
namespace dali
{
// Factory presets embedded into the binary at build time (Resources/FactoryPresets/*.dalicandy)
std::vector<std::pair<juce::String, juce::String>> embeddedFactoryPresets();
}
