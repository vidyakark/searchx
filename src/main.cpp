#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "searchx/corpus.hpp"
#include "searchx/engine.hpp"
#include "server.hpp"

using namespace searchx;

static int usage() {
    std::fputs(
        "searchx - a high-performance full-text search engine\n\n"
        "  searchx index  <corpus.tsv> <out.idx> [threads]      build an index (title<TAB>text per line)\n"
        "  searchx search <index.idx> \"query\" [k]               run one query\n"
        "  searchx serve  <index.idx> [port=8080] [threads=4] [host=127.0.0.1]\n\n"
        "Query syntax: a b (AND)   a OR b   a NOT b / a -b   \"exact phrase\"   (a OR b) c\n",
        stderr);
    return 2;
}

static std::unique_ptr<Index> loadOrDie(const char* path) {
    std::string err;
    auto ix = Index::load(path, &err);
    if (!ix) { std::fprintf(stderr, "error: %s: %s\n", path, err.c_str()); std::exit(1); }
    return ix;
}

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];

    if (cmd == "index" && argc >= 4) {
        auto t0 = std::chrono::steady_clock::now();
        std::vector<RawDoc> docs = loadTsv(argv[2]);
        if (docs.empty()) { std::fprintf(stderr, "error: no documents in %s\n", argv[2]); return 1; }
        auto ix = Index::build(docs, argc >= 5 ? std::atoi(argv[4]) : 0);
        if (!ix->save(argv[3])) { std::fprintf(stderr, "error: cannot write %s\n", argv[3]); return 1; }
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        double raw = double(ix->totalPostings()) * 8 + double(ix->totalTokens()) * 4;  // uncompressed estimate
        std::printf("indexed %u docs, %zu terms, %llu postings in %.2fs\n", ix->numDocs(), ix->numTerms(),
                    (unsigned long long)ix->totalPostings(), s);
        std::printf("postings: %.1f MB compressed vs %.1f MB raw (%.1fx smaller)\n", ix->postingsBytes() / 1e6, raw / 1e6,
                    raw / std::max<size_t>(1, ix->postingsBytes()));
        return 0;
    }

    if (cmd == "search" && argc >= 4) {
        auto ix = loadOrDie(argv[2]);
        Engine eng(*ix, 0);
        SearchResult r = eng.search(argv[3], argc >= 5 ? std::atoi(argv[4]) : 10);
        std::printf("%llu matches in %.3f ms\n", (unsigned long long)r.totalMatches, r.micros / 1000.0);
        for (const Hit& h : r.hits) std::printf("  %7.3f  #%-8u %s\n          %s\n", h.score, h.doc, ix->title(h.doc).c_str(), ix->snippet(h.doc).c_str());
        return 0;
    }

    if (cmd == "serve" && argc >= 3) {
        auto ix = loadOrDie(argv[2]);
        Engine eng(*ix, 4096);
        int port = argc >= 4 ? std::atoi(argv[3]) : 8080;
        int threads = argc >= 5 ? std::atoi(argv[4]) : 4;
        std::string host = argc >= 6 ? argv[5] : "127.0.0.1";
        return runServer(eng, host, port, threads);
    }
    return usage();
}
