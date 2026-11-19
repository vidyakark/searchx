#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace searchx {

struct Token {
    std::string term;  // normalized (lowercased + stemmed)
    uint32_t pos;      // word position in the original text (stop words still advance it)
};

// Lightweight suffix-stripping stemmer (plural / -ing / -ed / -ly).
std::string stem(std::string word);
bool isStopword(const std::string& word);

// Splits ASCII alphanumeric words, lowercases, drops stop words, stems.
// Positions count *all* words, so phrase queries like "over the lazy" still line up.
void tokenize(std::string_view text, std::vector<Token>& out);

}  // namespace searchx
