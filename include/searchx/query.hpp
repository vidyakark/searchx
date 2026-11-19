#pragma once
#include <memory>
#include <vector>

#include "searchx/index.hpp"
#include "searchx/tokenizer.hpp"

namespace searchx {

// Query grammar:   expr := and ("OR" and)* ;  and := unary (["AND"] unary)* ;
//                  unary := ("NOT" | "-") unary | "(" expr ")" | "phrase words" | word
// Adjacent terms mean AND. Operators are upper-case so "and"/"or" stay ordinary stop words.
struct Node {
    enum Kind { Term, Phrase, And, Or, Not } kind;
    std::vector<Token> toks;                  // Term: 1 token, Phrase: n tokens
    std::vector<std::unique_ptr<Node>> kids;  // And / Or / Not
};
using NodePtr = std::unique_ptr<Node>;

struct Hit {
    uint32_t doc;
    float score;
};

NodePtr parseQuery(const std::string& q);                                  // nullptr if empty
std::vector<uint32_t> evaluate(const Index& ix, const Node& n);            // sorted matching doc ids
std::vector<Hit> rankBM25(const Index& ix, const Node& root,
                          const std::vector<uint32_t>& cands, size_t k);   // top-k by BM25

}  // namespace searchx
