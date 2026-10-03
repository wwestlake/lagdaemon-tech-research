#include "ElectronicsKnowledge.h"

#include <algorithm>
#include <map>
#include <set>

namespace electronics_knowledge
{
namespace
{
juce::StringArray queryTokens(const juce::String& query)
{
    const auto lowerQuery = query.toLowerCase();
    juce::StringArray rawTokens;

    if (lowerQuery.contains("erc") || lowerQuery.contains("electrical rule"))
    {
        rawTokens.add("erc");
        rawTokens.add("electrical_rule_check");
    }
    if (lowerQuery.contains("scope") || lowerQuery.contains("oscilloscope"))
    {
        rawTokens.add("oscilloscope");
        rawTokens.add("instrument");
    }
    if (lowerQuery.contains("meter") || lowerQuery.contains("multimeter"))
    {
        rawTokens.add("multimeter");
        rawTokens.add("instrument");
    }
    if (lowerQuery.contains("spice") || lowerQuery.contains("xyce"))
    {
        rawTokens.add("xyce");
        rawTokens.add("solver");
    }

    rawTokens.addTokens(lowerQuery, " \t\r\n.,!?;:()[]{}<>+-=*/\\|&^%\"'", "");
    rawTokens.trim();
    rawTokens.removeEmptyStrings();

    static const std::set<std::string> stopWords {
        "about", "after", "also", "and", "are", "can", "does", "for", "from",
        "have", "how", "into", "like", "make", "need", "that", "the", "this",
        "use", "want", "what", "when", "where", "with", "you"
    };

    juce::StringArray tokens;
    std::set<std::string> seen;
    for (auto& token : rawTokens)
    {
        token = token.retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789_-.").trim();
        if (token.length() <= 1)
            continue;

        const auto stdToken = token.toStdString();
        if (stopWords.count(stdToken) != 0 || seen.count(stdToken) != 0)
            continue;

        seen.insert(stdToken);
        tokens.add(token);
        if (tokens.size() >= 16)
            break;
    }

    return tokens;
}

juce::String trimForPrompt(juce::String text)
{
    text = text.trim();
    if (text.length() <= 1200)
        return text;

    return text.substring(0, 1200).trim() + "\n...";
}

juce::StringArray arrayProperty(const juce::var& value)
{
    juce::StringArray result;
    if (auto* array = value.getArray())
    {
        for (const auto& item : *array)
            result.add(item.toString());
    }
    result.removeEmptyStrings();
    return result;
}

Card cardFromJson(const juce::var& parsed, const juce::File& sourceFile, const juce::String& rawJson)
{
    Card card;
    card.id = parsed.getProperty("id", {}).toString();
    card.kind = parsed.getProperty("kind", {}).toString();
    card.title = parsed.getProperty("title", card.id).toString();
    card.source = parsed.getProperty("source", sourceFile.getFullPathName()).toString();
    card.text = parsed.getProperty("text", {}).toString();
    card.rawJson = rawJson;
    card.tokens = arrayProperty(parsed.getProperty("tokens", {}));
    card.priority = static_cast<int>(parsed.getProperty("priority", 50));
    return card;
}

Card cardFromCapabilityGapJson(const juce::var& parsed, const juce::File& sourceFile, const juce::String& rawJson)
{
    const auto id = parsed.getProperty("id", {}).toString();
    const auto category = parsed.getProperty("category", "unspecified").toString();
    const auto status = parsed.getProperty("status", "open").toString();
    const auto description = parsed.getProperty("description", {}).toString();
    const auto neededCapability = parsed.getProperty("neededCapability", {}).toString();
    const auto evidence = parsed.getProperty("evidence", {}).toString();
    const auto source = parsed.getProperty("source", {}).toString();

    Card card;
    card.id = id.isNotEmpty() ? id : "gap." + juce::String(rawJson.hashCode());
    card.kind = "capability_gap";
    card.title = "Capability Gap: " + category;
    card.source = sourceFile.getFullPathName();
    card.rawJson = rawJson;
    card.priority = status.equalsIgnoreCase("open") ? 97 : 70;
    card.tokens.add("capability_gap");
    card.tokens.add("gap");
    card.tokens.add(category);
    card.tokens.add(status);
    card.tokens.addTokens(neededCapability.toLowerCase(), " \t\r\n.,!?;:()[]{}<>+-=*/\\|&^%\"'", "");
    card.tokens.trim();
    card.tokens.removeEmptyStrings();
    card.tokens.removeDuplicates(false);
    card.text = "Capability gap (" + status + ", " + category + "): " + description
        + "\nNeeded capability: " + neededCapability;
    if (evidence.isNotEmpty())
        card.text << "\nEvidence: " << evidence;
    if (source.isNotEmpty())
        card.text << "\nSource: " << source;
    return card;
}

std::vector<Card> loadCards(const juce::File& file)
{
    std::vector<Card> cards;
    if (!file.existsAsFile())
        return cards;

    juce::StringArray lines;
    lines.addLines(file.loadFileAsString());
    for (const auto& rawLine : lines)
    {
        const auto line = rawLine.trim();
        if (line.isEmpty())
            continue;

        const auto parsed = juce::JSON::parse(line);
        if (!parsed.isObject())
            continue;

        const auto status = parsed.getProperty("status", "verified").toString();
        if (status.equalsIgnoreCase("inactive") || status.equalsIgnoreCase("deprecated"))
            continue;

        auto card = cardFromJson(parsed, file, line);
        if (card.id.isNotEmpty() && card.text.isNotEmpty())
            cards.push_back(std::move(card));
    }

    return cards;
}

std::vector<Card> loadCapabilityGapCards(const juce::File& file)
{
    std::vector<Card> cards;
    if (!file.existsAsFile())
        return cards;

    juce::StringArray lines;
    lines.addLines(file.loadFileAsString());
    for (const auto& rawLine : lines)
    {
        const auto line = rawLine.trim();
        if (line.isEmpty())
            continue;

        const auto parsed = juce::JSON::parse(line);
        if (!parsed.isObject())
            continue;

        const auto kind = parsed.getProperty("kind", {}).toString();
        if (!kind.equalsIgnoreCase("djehuti_capability_gap"))
            continue;

        auto card = cardFromCapabilityGapJson(parsed, file, line);
        if (card.id.isNotEmpty() && card.text.isNotEmpty())
            cards.push_back(std::move(card));
    }

    return cards;
}

std::vector<Card> loadAllCards()
{
    std::vector<Card> cards;
    const auto cardsDirectory = getCardsDirectory();
    juce::Array<juce::File> files;
    cardsDirectory.findChildFiles(files, juce::File::findFiles, true, "*.jsonl");
    files.sort();

    for (const auto& file : files)
    {
        auto loaded = loadCards(file);
        cards.insert(cards.end(),
                     std::make_move_iterator(loaded.begin()),
                     std::make_move_iterator(loaded.end()));
    }

    auto projectCards = loadCards(getProjectMemoryCardsFile());
    cards.insert(cards.end(),
                 std::make_move_iterator(projectCards.begin()),
                 std::make_move_iterator(projectCards.end()));
    auto gapCards = loadCapabilityGapCards(getCapabilityGapsFile());
    cards.insert(cards.end(),
                 std::make_move_iterator(gapCards.begin()),
                 std::make_move_iterator(gapCards.end()));
    return cards;
}

double scoreCard(const Card& card, const juce::StringArray& tokens)
{
    if (tokens.isEmpty())
        return 0.0;

    juce::String haystack;
    haystack << card.id << " " << card.kind << " " << card.title << " " << card.text;
    for (const auto& token : card.tokens)
        haystack << " " << token;

    const auto lower = haystack.toLowerCase();
    double score = 0.0;
    for (const auto& token : tokens)
    {
        const auto loweredToken = token.toLowerCase();
        if (card.title.toLowerCase().contains(loweredToken))
            score += 5.0;
        if (card.tokens.contains(loweredToken, true))
            score += 4.0;
        if (lower.contains(loweredToken))
            score += 1.0;
    }

    if (card.kind.containsIgnoreCase("process"))
        score *= 1.1;
    return score + static_cast<double>(card.priority) * 0.01;
}
}

juce::File getKnowledgeRoot()
{
    return juce::File(ELECTRONICS_RESEARCH_ROOT)
        .getChildFile("prototype")
        .getChildFile("knowledge");
}

juce::File getCardsDirectory()
{
    return getKnowledgeRoot().getChildFile("cards");
}

juce::File getProjectMemoryCardsFile()
{
    return juce::File(ELECTRONICS_RESEARCH_ROOT)
        .getChildFile("prototype")
        .getChildFile("projects")
        .getChildFile("current")
        .getChildFile(".djehuti")
        .getChildFile("MEMORY_PROJECT_CARDS.jsonl");
}

juce::File getCapabilityGapsFile()
{
    return juce::File(ELECTRONICS_RESEARCH_ROOT)
        .getChildFile("prototype")
        .getChildFile("projects")
        .getChildFile("current")
        .getChildFile(".djehuti")
        .getChildFile("CAPABILITY_GAPS.jsonl");
}

std::vector<Card> allCards()
{
    return loadAllCards();
}

RetrievalResult retrieve(const juce::String& query, int maxCards)
{
    RetrievalResult result;
    result.query = query;
    result.tokens = queryTokens(query);
    if (result.tokens.isEmpty())
        return result;

    struct ScoredCard
    {
        Card card;
        double score = 0.0;
    };

    std::vector<ScoredCard> scored;
    for (const auto& card : loadAllCards())
    {
        const auto score = scoreCard(card, result.tokens);
        if (score > 0.0)
            scored.push_back({ card, score });
    }

    std::stable_sort(scored.begin(), scored.end(), [](const auto& left, const auto& right) {
        if (left.score == right.score)
            return left.card.id < right.card.id;
        return left.score > right.score;
    });

    const auto limit = std::max(0, maxCards);
    for (int index = 0; index < limit && index < static_cast<int>(scored.size()); ++index)
        result.cards.push_back(std::move(scored[(size_t)index].card));

    if (!result.cards.empty())
    {
        result.context = "Relevant Djehuti Electronics Lab Context (filesystem LiteSemRAG cards):\n";
        for (const auto& card : result.cards)
        {
            result.context << "\n--- [" << card.kind << "] " << card.title;
            if (card.source.isNotEmpty())
                result.context << " (" << card.source << ")";
            result.context << " ---\n" << trimForPrompt(card.text) << "\n";
        }
    }

    return result;
}

juce::String contextForQuery(const juce::String& query, int maxCards)
{
    return retrieve(query, maxCards).context;
}
}
