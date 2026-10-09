#include "ElectronicsKnowledge.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace electronics_knowledge
{
namespace
{
// ---- Retrieval: BM25F over whole words ------------------------------------
// Cards and queries are cut into whole words (identifiers such as
// opamp_generic are kept and also split into their parts), common words are
// dropped, and a trailing plural "s" is folded so "capacitors" finds
// "capacitor". Each card is scored with BM25F over its title, keywords, id
// and text: a word found in few cards (chua, negative, impedance) weighs far
// more than one found in many (current, project, design). Words that sit next
// to each other in the request and in a card (a phrase such as "negative
// impedance converter") earn a bonus.

const std::set<std::string>& stopWords()
{
    static const std::set<std::string> words {
        "a", "about", "above", "after", "again", "all", "also", "am", "an", "and", "any", "are", "as", "at", "be", "been",
        "before", "being", "both", "but", "by", "can", "could", "did", "do", "does", "doing", "done", "each", "else", "etc",
        "even", "every", "few", "for", "from", "further", "get", "give", "go", "had", "has", "have", "having", "he", "her",
        "here", "him", "his", "how", "i", "if", "in", "into", "is", "it", "its", "just", "let", "like", "make", "many", "may",
        "me", "might", "more", "most", "much", "must", "my", "need", "no", "nor", "not", "now", "of", "off", "on", "once",
        "only", "or", "other", "our", "out", "over", "own", "please", "same", "she", "should", "so", "some", "such", "than",
        "that", "the", "their", "them", "then", "there", "these", "they", "this", "those", "through", "to", "too", "under",
        "until", "up", "us", "use", "using", "very", "want", "was", "we", "were", "what", "when", "where", "which", "while",
        "who", "why", "will", "with", "would", "yes", "you", "your", "yours"
    };
    return words;
}

std::string fold(std::string w)
{
    const auto n = w.size();
    if (n > 4 && w[n - 1] == 's' && w[n - 2] != 's' && w[n - 2] != 'u' && w[n - 2] != 'i')
        w.pop_back();
    return w;
}

// Words in order; an identifier with underscores is followed by its parts.
// `gapBefore` marks words that did not directly follow the previous kept word
// (a stop word or a field break sat between), so phrases never span them.
struct WordStream
{
    std::vector<std::string> words;
    std::vector<bool> gapBefore;
};

void addWords(WordStream& out, const juce::String& text)
{
    const auto lower = text.toLowerCase().toStdString();
    bool gap = true;
    std::string current;
    auto flush = [&] {
        if (current.empty())
            return;
        std::string word = current;
        current.clear();
        while (!word.empty() && word.front() == '_') word.erase(word.begin());
        while (!word.empty() && word.back() == '_') word.pop_back();
        if (word.size() <= 1 && !(word.size() == 1 && std::isdigit((unsigned char)word[0])))
        {
            gap = true;
            return;
        }
        if (stopWords().count(word) != 0)
        {
            gap = true;
            return;
        }
        const auto folded = fold(word);
        out.words.push_back(folded);
        out.gapBefore.push_back(gap);
        gap = false;
        if (word.find('_') != std::string::npos)
        {
            size_t start = 0;
            while (start <= word.size())
            {
                const auto end = word.find('_', start);
                const auto part = word.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (part.size() > 1 && stopWords().count(part) == 0)
                {
                    out.words.push_back(fold(part));
                    out.gapBefore.push_back(false);
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
    };
    for (char c : lower)
    {
        if (std::isalnum((unsigned char)c) || c == '_')
            current.push_back(c);
        else
        {
            flush();
            if (c != ' ' && c != '-')
                gap = true; // punctuation breaks a phrase; spaces and hyphens do not
        }
    }
    flush();
}

WordStream wordsOf(const juce::String& text)
{
    WordStream s;
    addWords(s, text);
    return s;
}

// Query words, plus the same synonyms the old matcher added for common asks.
juce::StringArray queryTokens(const juce::String& query)
{
    const auto lowerQuery = query.toLowerCase();
    juce::StringArray tokens;
    std::set<std::string> seen;
    auto add = [&](const std::string& w) {
        if (seen.insert(w).second)
            tokens.add(juce::String(w));
    };
    for (const auto& w : wordsOf(query).words)
        add(w);
    if (lowerQuery.contains("erc") || lowerQuery.contains("electrical rule"))
        for (auto w : { "erc", "electrical_rule_check" }) add(w);
    if (lowerQuery.contains("scope"))
        for (auto w : { "oscilloscope", "instrument" }) add(w);
    if (lowerQuery.contains("meter"))
        for (auto w : { "multimeter", "instrument" }) add(w);
    if (lowerQuery.contains("spice") || lowerQuery.contains("xyce"))
        for (auto w : { "xyce", "solver" }) add(w);
    return tokens;
}

struct IndexedCard
{
    std::map<std::string, double> weightedTf; // field-weighted term frequency
    std::set<std::pair<std::string, std::string>> pairs; // adjacent word pairs
    double length = 0.0;
};

constexpr double titleWeight = 3.0, keywordWeight = 2.0, idWeight = 1.0, textWeight = 1.0;

IndexedCard indexCard(const Card& card)
{
    IndexedCard ix;
    auto field = [&](const juce::String& text, double weight) {
        const auto s = wordsOf(text);
        for (size_t i = 0; i < s.words.size(); ++i)
        {
            ix.weightedTf[s.words[i]] += weight;
            ix.length += weight;
            if (i > 0 && !s.gapBefore[i])
                ix.pairs.insert({ s.words[i - 1], s.words[i] });
        }
    };
    field(card.title, titleWeight);
    for (const auto& keyword : card.tokens)
        field(keyword, keywordWeight);
    field(card.id.replaceCharacter('.', ' ').replaceCharacter('#', ' '), idWeight);
    field(card.text, textWeight);
    return ix;
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

namespace
{
juce::File& projectMemoryDirectory()
{
    static juce::File directory;
    return directory;
}

juce::File memoryDirectory()
{
    if (projectMemoryDirectory() != juce::File())
        return projectMemoryDirectory();
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiElectronicsLab")
        .getChildFile("memory");
}
}

void setProjectMemoryDirectory(const juce::File& directory)
{
    projectMemoryDirectory() = directory;
}

juce::File getProjectMemoryCardsFile()
{
    return memoryDirectory().getChildFile("MEMORY_PROJECT_CARDS.jsonl");
}

juce::File getCapabilityGapsFile()
{
    return memoryDirectory().getChildFile("CAPABILITY_GAPS.jsonl");
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
    const auto cards = loadAllCards();
    std::vector<IndexedCard> index;
    index.reserve(cards.size());
    std::map<std::string, int> documentFrequency;
    double totalLength = 0.0;
    for (const auto& card : cards)
    {
        index.push_back(indexCard(card));
        for (const auto& entry : index.back().weightedTf)
            ++documentFrequency[entry.first];
        totalLength += index.back().length;
    }
    const auto n = (double)std::max<size_t>(1, cards.size());
    const auto averageLength = std::max(1.0, totalLength / n);
    auto idf = [&](const std::string& word) {
        const auto found = documentFrequency.find(word);
        const auto df = found == documentFrequency.end() ? 0.0 : (double)found->second;
        return std::log(1.0 + (n - df + 0.5) / (df + 0.5));
    };

    std::vector<std::string> queryWords;
    for (const auto& token : result.tokens)
        queryWords.push_back(token.toStdString());
    std::set<std::pair<std::string, std::string>> queryPairs;
    const auto stream = wordsOf(query);
    for (size_t i = 1; i < stream.words.size(); ++i)
        if (!stream.gapBefore[i] && stream.words[i - 1] != stream.words[i])
            queryPairs.insert({ stream.words[i - 1], stream.words[i] });

    constexpr double k1 = 1.2, b = 0.75;
    for (size_t c = 0; c < cards.size(); ++c)
    {
        const auto& ix = index[c];
        double score = 0.0;
        for (const auto& word : queryWords)
        {
            const auto found = ix.weightedTf.find(word);
            if (found == ix.weightedTf.end())
                continue;
            const auto tf = found->second;
            score += idf(word) * tf * (k1 + 1.0) / (tf + k1 * (1.0 - b + b * ix.length / averageLength));
        }
        if (score <= 0.0)
            continue;
        for (const auto& pair : queryPairs)
            if (ix.pairs.count(pair) != 0)
                score += 0.5 * (idf(pair.first) + idf(pair.second));
        if (cards[c].kind.containsIgnoreCase("process"))
            score *= 1.1;
        scored.push_back({ cards[c], score + cards[c].priority * 0.001 });
    }

    std::stable_sort(scored.begin(), scored.end(), [](const auto& left, const auto& right) {
        if (left.score == right.score)
            return left.card.id < right.card.id;
        return left.score > right.score;
    });
    // Leave out cards that match only incidentally (a common word or two)
    // when much better matches exist.
    if (!scored.empty())
    {
        const auto floor = scored.front().score * 0.3;
        scored.erase(std::remove_if(scored.begin(), scored.end(), [floor](const auto& s) { return s.score < floor; }), scored.end());
    }


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
