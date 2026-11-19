#include <algorithm>
#include <cstdio>
#include <random>
#include <set>
#include <string>

#include "searchx/engine.hpp"
#include "searchx/varbyte.hpp"

using namespace searchx;
using Ids = std::vector<uint32_t>;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static Ids run(const Index& ix, const std::string& q) {
    NodePtr n = parseQuery(q);
    return n ? evaluate(ix, *n) : Ids{};
}

static void testVarbyte() {
    std::mt19937 rng(1);
    std::vector<uint32_t> vals = {0, 1, 127, 128, 16383, 16384, 0xFFFFFFFFu};
    for (int i = 0; i < 10000; ++i) vals.push_back(rng() >> (rng() % 32));
    std::vector<uint8_t> buf;
    for (uint32_t v : vals) vbEncode(v, buf);
    const uint8_t* p = buf.data();
    bool ok = true;
    for (uint32_t v : vals) ok &= (vbDecode(p) == v);
    CHECK(ok);
    CHECK(p == buf.data() + buf.size());
}

static void testTokenizer() {
    std::vector<Token> t;
    tokenize("The Quick fox, searching QUERIES!", t);
    CHECK(t.size() == 4);
    CHECK(t[0].term == "quick" && t[0].pos == 1);  // "the" is a stop word but still advances positions
    CHECK(t[2].term == "search");
    CHECK(t[3].term == "query");
    CHECK(stem("engines") == "engine");
}

static std::unique_ptr<Index> smallIndex() {
    std::vector<RawDoc> d = {
        {"d0", "the quick brown fox jumps over the lazy dog"},
        {"d1", "quick quick quick brown bear"},
        {"d2", "a lazy afternoon with the dog"},
        {"d3", "brown fox and quick dog"},
    };
    return Index::build(d, 2);
}

static void testQueries() {
    auto ix = smallIndex();
    CHECK(run(*ix, "quick brown") == (Ids{0, 1, 3}));
    CHECK(run(*ix, "quick OR lazy") == (Ids{0, 1, 2, 3}));
    CHECK(run(*ix, "quick NOT fox") == (Ids{1}));
    CHECK(run(*ix, "quick -fox") == (Ids{1}));
    CHECK(run(*ix, "NOT quick") == (Ids{2}));
    CHECK(run(*ix, "(quick OR lazy) AND dog") == (Ids{0, 2, 3}));
    CHECK(run(*ix, "\"brown fox\"") == (Ids{0, 3}));
    CHECK(run(*ix, "\"fox jumps\"") == (Ids{0}));
    CHECK(run(*ix, "\"lazy dog\"") == (Ids{0}));
    CHECK(run(*ix, "\"over the lazy\"") == (Ids{0}));  // stop-word gap preserved
    CHECK(run(*ix, "\"over lazy\"").empty());
    CHECK(run(*ix, "nonexistentterm").empty());
    CHECK(run(*ix, "quick nonexistentterm").empty());
    CHECK(run(*ix, "the").empty());  // stop word only
    CHECK(run(*ix, "((( ) \"").empty());  // malformed input must not crash or hang

    Engine eng(*ix, 16);
    SearchResult r = eng.search("quick", 10);
    CHECK(r.totalMatches == 3 && r.hits.size() == 3);
    CHECK(r.hits[0].doc == 1);  // highest term frequency in a short doc ranks first
    CHECK(!r.cached);
    CHECK(eng.search("quick", 10).cached);
    CHECK(eng.search("quick", 2).hits.size() == 2);
}

static void testSkipPointers() {
    const uint32_t N = 20000;
    std::vector<RawDoc> d;
    for (uint32_t i = 0; i < N; ++i) {
        std::string t = "common";
        if (i % 3 == 0) t += " three";
        if (i % 7 == 0) t += " seven";
        if (i % 5000 == 0) t += " rare";
        d.push_back({"x", t});
    }
    auto ix = Index::build(d, 4);

    Ids expect;
    for (uint32_t i = 0; i < N; ++i) if (i % 21 == 0) expect.push_back(i);
    CHECK(run(*ix, "three seven common") == expect);
    CHECK(run(*ix, "rare three").size() == 2);  // docs 0 and 15000

    // advance() must agree with brute force for random increasing targets.
    const TermInfo* ti = ix->find("three");
    CHECK(ti && ti->df == (N + 2) / 3 && ti->nskips == (ti->df + kSkipInterval - 1) / kSkipInterval);
    std::mt19937 rng(3);
    PostingIter it = ix->iter(*ti);
    bool ok = true;
    uint32_t target = 0;
    while (true) {
        target += rng() % 500;
        uint32_t want = (target + 2) / 3 * 3;
        if (want >= N) break;
        ok &= it.advance(target) && it.doc() == want;
    }
    CHECK(ok);
}

static void testPersistence() {
    auto ix = smallIndex();
    const char* path = "/tmp/searchx_test.idx";
    CHECK(ix->save(path));
    std::string err;
    auto ix2 = Index::load(path, &err);
    CHECK(ix2 != nullptr);
    if (!ix2) return;
    CHECK(ix2->numDocs() == ix->numDocs() && ix2->numTerms() == ix->numTerms());
    for (const char* q : {"quick brown", "quick OR lazy", "\"brown fox\"", "NOT quick"}) CHECK(run(*ix, q) == run(*ix2, q));
    CHECK(ix2->title(1) == "d1");
    CHECK(Index::load("/tmp/does_not_exist.idx", &err) == nullptr);
    std::remove(path);
}

int main() {
    testVarbyte();
    testTokenizer();
    testQueries();
    testSkipPointers();
    testPersistence();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
