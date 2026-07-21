#include "HelpText.h"
#include "BinaryData.h"
#include <unordered_map>
#include <string>

namespace
{
    // Lazy-built map from help.json key -> description (the LAM loader).
    // Function-local static: parsed once on first use, lives for the process.
    const std::unordered_map<std::string, juce::String>& helpTable()
    {
        static const std::unordered_map<std::string, juce::String> table = []()
        {
            std::unordered_map<std::string, juce::String> out;
            const juce::String jsonText (BinaryData::help_json,
                                         (size_t) BinaryData::help_jsonSize);
            const auto parsed = juce::JSON::parse (jsonText);
            if (auto* obj = parsed.getDynamicObject())
            {
                for (const auto& kv : obj->getProperties())
                {
                    // "_comment" is file-level documentation, not a lookup key.
                    const auto key = kv.name.toString();
                    if (key == "_comment")
                        continue;
                    out.emplace (key.toStdString(), kv.value.toString());
                }
            }
            return out;
        }();
        return table;
    }
}

juce::String HelpText::descriptionFor (juce::StringRef key)
{
    if (key.isEmpty())
        return {};
    const auto& t = helpTable();
    const auto it = t.find (juce::String (key).toStdString());
    return it != t.end() ? it->second : juce::String();
}

juce::String HelpText::keyForOption (const juce::String& baseKey,
                                     const juce::String& optionText)
{
    const auto candidate = baseKey + "."
                         + optionText.toLowerCase().replaceCharacter (' ', '-');
    return helpTable().count (candidate.toStdString()) > 0 ? candidate : baseKey;
}
