#include "searchx/tokenizer.hpp"

#include <cctype>
#include <cstring>
#include <unordered_set>

namespace searchx {

bool isStopword(const std::string& w) {
    static const std::unordered_set<std::string> kStop = {
        "a", "an", "and", "are", "as", "at", "be", "but", "by", "for", "if", "in", "into", "is",
        "it", "no", "not", "of", "on", "or", "such", "that", "the", "their", "then", "there",
        "these", "they", "this", "to", "was", "will", "with"};
    return kStop.count(w) != 0;
}

std::string stem(std::string w) {
    auto ends = [&](const char* s) {
        size_t n = std::strlen(s);
        return w.size() >= n && w.compare(w.size() - n, n, s) == 0;
    };
    if (w.size() <= 3) return w;
    if (ends("sses")) w.resize(w.size() - 2);
    else if (ends("ies") && w.size() > 4) { w.resize(w.size() - 3); w += 'y'; }
    else if (ends("ss")) {}
    else if (ends("s") && !ends("us") && !ends("is")) w.pop_back();

    if (w.size() > 5 && ends("ing")) w.resize(w.size() - 3);
    else if (w.size() > 4 && ends("ed")) w.resize(w.size() - 2);
    else if (w.size() > 5 && ends("ly")) w.resize(w.size() - 2);
    return w;
}

void tokenize(std::string_view text, std::vector<Token>& out) {
    out.clear();
    std::string w;
    uint32_t pos = 0;
    auto flush = [&] {
        if (w.empty()) return;
        if (w.size() <= 40 && !isStopword(w)) out.push_back({stem(w), pos});
        ++pos;
        w.clear();
    };
    for (unsigned char c : text) {
        if (std::isalnum(c)) w.push_back(static_cast<char>(std::tolower(c)));
        else flush();
    }
    flush();
}

}  // namespace searchx
