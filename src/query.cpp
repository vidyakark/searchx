#include "searchx/query.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>

namespace searchx {
namespace {

using DocList = std::vector<uint32_t>;

// ---------------------------------------------------------------- parsing
struct Lex {
    enum T { Word, Phrase, And, Or, Not, LP, RP } t;
    std::string s;
};

std::vector<Lex> lexQuery(const std::string& q) {
    std::vector<Lex> out;
    size_t i = 0, n = q.size();
    while (i < n) {
        char c = q[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '(') { out.push_back({Lex::LP, ""}); ++i; continue; }
        if (c == ')') { out.push_back({Lex::RP, ""}); ++i; continue; }
        if (c == '"') {
            size_t j = q.find('"', i + 1);
            if (j == std::string::npos) j = n;
            out.push_back({Lex::Phrase, q.substr(i + 1, j - i - 1)});
            i = j + 1;
            continue;
        }
        if (c == '-' && i + 1 < n && !std::isspace(static_cast<unsigned char>(q[i + 1]))) {
            out.push_back({Lex::Not, ""});
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && !std::isspace(static_cast<unsigned char>(q[j])) && q[j] != '(' && q[j] != ')' && q[j] != '"') ++j;
        std::string w = q.substr(i, j - i);
        i = j;
        if (w == "AND") out.push_back({Lex::And, ""});
        else if (w == "OR") out.push_back({Lex::Or, ""});
        else if (w == "NOT") out.push_back({Lex::Not, ""});
        else out.push_back({Lex::Word, w});
    }
    return out;
}

NodePtr mk(Node::Kind k) {
    auto n = std::make_unique<Node>();
    n->kind = k;
    return n;
}

NodePtr fromText(const std::string& s) {
    std::vector<Token> toks;
    tokenize(s, toks);
    if (toks.empty()) return nullptr;  // stop-word only
    NodePtr n = mk(toks.size() == 1 ? Node::Term : Node::Phrase);
    n->toks = std::move(toks);
    return n;
}

NodePtr combine(Node::Kind k, std::vector<NodePtr> kids) {
    kids.erase(std::remove(kids.begin(), kids.end(), nullptr), kids.end());
    if (kids.empty()) return nullptr;
    if (kids.size() == 1) return std::move(kids[0]);
    NodePtr n = mk(k);
    n->kids = std::move(kids);
    return n;
}

class Parser {
public:
    explicit Parser(std::vector<Lex> t) : t_(std::move(t)) {}
    NodePtr parse() { return parseOr(); }

private:
    bool peek(Lex::T x) const { return i_ < t_.size() && t_[i_].t == x; }

    NodePtr parseOr() {
        std::vector<NodePtr> alts;
        alts.push_back(parseAnd());
        while (peek(Lex::Or)) { ++i_; alts.push_back(parseAnd()); }
        return combine(Node::Or, std::move(alts));
    }
    NodePtr parseAnd() {
        std::vector<NodePtr> kids;
        while (i_ < t_.size()) {
            if (peek(Lex::Or)) break;
            if (peek(Lex::RP)) { if (depth_ > 0) break; ++i_; continue; }
            if (peek(Lex::And)) { ++i_; continue; }
            kids.push_back(parseUnary());
        }
        return combine(Node::And, std::move(kids));
    }
    NodePtr parseUnary() {  // always consumes at least one token
        if (i_ >= t_.size()) return nullptr;
        Lex l = t_[i_++];
        switch (l.t) {
            case Lex::Not: {
                NodePtr c = parseUnary();
                if (!c) return nullptr;
                NodePtr n = mk(Node::Not);
                n->kids.push_back(std::move(c));
                return n;
            }
            case Lex::LP: {
                ++depth_;
                NodePtr e = parseOr();
                --depth_;
                if (peek(Lex::RP)) ++i_;
                return e;
            }
            case Lex::Word:
            case Lex::Phrase: return fromText(l.s);
            default: return nullptr;
        }
    }

    std::vector<Lex> t_;
    size_t i_ = 0;
    int depth_ = 0;
};

// ------------------------------------------------------------- evaluation
DocList decodeAll(const Index& ix, const std::string& term) {
    DocList out;
    const TermInfo* ti = ix.find(term);
    if (!ti) return out;
    out.reserve(ti->df);
    for (PostingIter it = ix.iter(*ti); it.valid(); it.next()) out.push_back(it.doc());
    return out;
}

// Intersect a (usually small) candidate list with a term's postings, skipping with skip pointers.
DocList intersectTerm(const Index& ix, const DocList& cur, const TermInfo& ti) {
    DocList out;
    PostingIter it = ix.iter(ti);
    for (uint32_t d : cur) {
        if (!it.advance(d)) break;
        if (it.doc() == d) out.push_back(d);
    }
    return out;
}

DocList diffTerm(const Index& ix, const DocList& cur, const TermInfo& ti) {
    DocList out;
    PostingIter it = ix.iter(ti);
    bool alive = true;
    for (uint32_t d : cur) {
        if (alive) alive = it.advance(d);
        if (alive && it.doc() == d) continue;
        out.push_back(d);
    }
    return out;
}

DocList allDocs(const Index& ix) {
    DocList v(ix.numDocs());
    for (uint32_t i = 0; i < v.size(); ++i) v[i] = i;
    return v;
}

DocList evalPhrase(const Index& ix, const Node& n) {
    std::vector<PostingIter> its;
    std::vector<uint32_t> offs;
    for (const Token& t : n.toks) {
        const TermInfo* ti = ix.find(t.term);
        if (!ti) return {};
        its.push_back(ix.iter(*ti));
        offs.push_back(t.pos - n.toks[0].pos);
    }
    DocList out;
    std::vector<uint32_t> starts, pi;
    uint32_t target = its[0].doc();
    for (;;) {  // leapfrog join over all terms' postings
        bool agree = true;
        for (auto& it : its) {
            if (!it.advance(target)) return out;
            if (it.doc() > target) { target = it.doc(); agree = false; break; }
        }
        if (!agree) continue;
        its[0].positions(starts);
        for (size_t i = 1; i < its.size() && !starts.empty(); ++i) {
            its[i].positions(pi);
            starts.erase(std::remove_if(starts.begin(), starts.end(),
                                        [&](uint32_t s) { return !std::binary_search(pi.begin(), pi.end(), s + offs[i]); }),
                         starts.end());
        }
        if (!starts.empty()) out.push_back(target);
        target += 1;
    }
}

DocList evalNode(const Index& ix, const Node& n);

DocList evalAnd(const Index& ix, const Node& n) {
    std::vector<const TermInfo*> terms;
    std::vector<const Node*> others, negs;
    for (auto& k : n.kids) {
        if (k->kind == Node::Not) negs.push_back(k->kids[0].get());
        else if (k->kind == Node::Term) {
            const TermInfo* ti = ix.find(k->toks[0].term);
            if (!ti) return {};  // a required term that doesn't exist => no matches
            terms.push_back(ti);
        } else others.push_back(k.get());
    }
    std::sort(terms.begin(), terms.end(), [](auto* a, auto* b) { return a->df < b->df; });

    DocList cur;
    bool have = false;
    for (const TermInfo* ti : terms) {  // rarest term first, then skip-intersect the rest
        if (!have) {
            have = true;
            cur.reserve(ti->df);
            for (PostingIter it = ix.iter(*ti); it.valid(); it.next()) cur.push_back(it.doc());
        }
        else cur = intersectTerm(ix, cur, *ti);
        if (cur.empty()) return cur;
    }
    for (const Node* o : others) {
        DocList r = evalNode(ix, *o);
        if (!have) { cur = std::move(r); have = true; }
        else { DocList m; std::set_intersection(cur.begin(), cur.end(), r.begin(), r.end(), std::back_inserter(m)); cur = std::move(m); }
        if (cur.empty()) return cur;
    }
    if (!have) cur = allDocs(ix);
    for (const Node* ng : negs) {
        if (cur.empty()) break;
        if (ng->kind == Node::Term) {
            if (const TermInfo* ti = ix.find(ng->toks[0].term)) cur = diffTerm(ix, cur, *ti);
        } else {
            DocList r = evalNode(ix, *ng), m;
            std::set_difference(cur.begin(), cur.end(), r.begin(), r.end(), std::back_inserter(m));
            cur = std::move(m);
        }
    }
    return cur;
}

DocList evalNode(const Index& ix, const Node& n) {
    switch (n.kind) {
        case Node::Term: return decodeAll(ix, n.toks[0].term);
        case Node::Phrase: return evalPhrase(ix, n);
        case Node::And: return evalAnd(ix, n);
        case Node::Or: {
            DocList acc;
            for (auto& k : n.kids) {
                DocList r = evalNode(ix, *k), m;
                std::set_union(acc.begin(), acc.end(), r.begin(), r.end(), std::back_inserter(m));
                acc = std::move(m);
            }
            return acc;
        }
        case Node::Not: {
            DocList r = evalNode(ix, *n.kids[0]), out;
            DocList all = allDocs(ix);
            std::set_difference(all.begin(), all.end(), r.begin(), r.end(), std::back_inserter(out));
            return out;
        }
    }
    return {};
}

void collectTerms(const Node& n, std::vector<std::string>& out) {
    switch (n.kind) {
        case Node::Term:
        case Node::Phrase: for (auto& t : n.toks) out.push_back(t.term); break;
        case Node::And:
        case Node::Or: for (auto& k : n.kids) collectTerms(*k, out); break;
        case Node::Not: break;  // negated terms don't contribute to the score
    }
}

}  // namespace

NodePtr parseQuery(const std::string& q) { return Parser(lexQuery(q)).parse(); }

std::vector<uint32_t> evaluate(const Index& ix, const Node& n) { return evalNode(ix, n); }

std::vector<Hit> rankBM25(const Index& ix, const Node& root, const DocList& cands, size_t k) {
    constexpr double k1 = 1.2, b = 0.75;
    std::vector<std::string> terms;
    collectTerms(root, terms);
    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());

    const double N = ix.numDocs(), avgdl = std::max(1.0, ix.avgDocLen());
    std::vector<float> score(cands.size(), 0.f);
    for (const std::string& t : terms) {
        const TermInfo* ti = ix.find(t);
        if (!ti) continue;
        const double idf = std::log(1.0 + (N - ti->df + 0.5) / (ti->df + 0.5));
        PostingIter it = ix.iter(*ti);
        for (size_t i = 0; i < cands.size(); ++i) {
            if (!it.advance(cands[i])) break;
            if (it.doc() != cands[i]) continue;
            const double tf = it.tf(), dl = ix.docLen(cands[i]);
            score[i] += static_cast<float>(idf * tf * (k1 + 1) / (tf + k1 * (1 - b + b * dl / avgdl)));
        }
    }

    std::vector<Hit> hits(cands.size());
    for (size_t i = 0; i < cands.size(); ++i) hits[i] = {cands[i], score[i]};
    auto better = [](const Hit& a, const Hit& b2) { return a.score != b2.score ? a.score > b2.score : a.doc < b2.doc; };
    if (hits.size() > k) {
        std::nth_element(hits.begin(), hits.begin() + k, hits.end(), better);
        hits.resize(k);
    }
    std::sort(hits.begin(), hits.end(), better);
    return hits;
}

}  // namespace searchx
