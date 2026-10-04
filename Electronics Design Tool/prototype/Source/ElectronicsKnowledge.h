#pragma once

#include <JuceHeader.h>

#include <vector>

namespace electronics_knowledge
{
struct Card
{
    juce::String id;
    juce::String kind;
    juce::String title;
    juce::String source;
    juce::String text;
    juce::String rawJson;
    juce::StringArray tokens;
    int priority = 50;
};

struct RetrievalResult
{
    juce::String query;
    juce::StringArray tokens;
    std::vector<Card> cards;
    juce::String context;
};

juce::File getKnowledgeRoot();
juce::File getCardsDirectory();
// The open project's memory folder; an empty File falls back to the
// per-user folder used when no project is open.
void setProjectMemoryDirectory(const juce::File& directory);
juce::File getProjectMemoryCardsFile();
juce::File getCapabilityGapsFile();
std::vector<Card> allCards();
RetrievalResult retrieve(const juce::String& query, int maxCards = 8);
juce::String contextForQuery(const juce::String& query, int maxCards = 8);
}
