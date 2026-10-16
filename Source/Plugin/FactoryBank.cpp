#include "FactoryBank.h"
#include "FactoryData.h"

namespace dali
{
std::vector<std::pair<juce::String, juce::String>> embeddedFactoryPresets()
{
    std::vector<std::pair<juce::String, juce::String>> v;
    for (int i = 0; i < DaliFactory::namedResourceListSize; ++i)
    {
        int size = 0;
        const char* data = DaliFactory::getNamedResource(DaliFactory::namedResourceList[i], size);
        if (data == nullptr || size <= 0) continue;
        const juce::String json = juce::String::fromUTF8(data, size);
        const auto name = juce::JSON::parse(json).getProperty("name", DaliFactory::originalFilenames[i]).toString();
        v.emplace_back(name, json);
    }
    return v;
}
}
