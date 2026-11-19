#pragma once
// Corpus format: one document per line, "title<TAB>text". A line without a tab is text only.
#include <fstream>
#include <string>
#include <vector>

#include "searchx/index.hpp"

namespace searchx {

inline std::vector<RawDoc> loadTsv(const std::string& path) {
    std::vector<RawDoc> docs;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) docs.push_back({"doc " + std::to_string(docs.size()), line});
        else docs.push_back({line.substr(0, tab), line.substr(tab + 1)});
    }
    return docs;
}

}  // namespace searchx
