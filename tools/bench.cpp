// Latency / throughput benchmark.   bench <index.idx> [numQueries=20000] [threads=4]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "searchx/engine.hpp"

using namespace searchx;

struct Stats { double qps, mean, p50, p95, p99; };

static Stats runWorkload(const Engine& eng, const std::vector<std::string>& work, unsigned threads) {
    std::vector<std::vector<double>> lat(threads);
    std::vector<std::thread> pool;
    auto t0 = std::chrono::steady_clock::now();
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            for (size_t i = t; i < work.size(); i += threads) lat[t].push_back(eng.search(work[i], 10).micros);
        });
    for (auto& th : pool) th.join();
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::vector<double> all;
    for (auto& v : lat) all.insert(all.end(), v.begin(), v.end());
    std::sort(all.begin(), all.end());
    double sum = 0;
    for (double x : all) sum += x;
    auto pct = [&](double p) { return all[std::min(all.size() - 1, size_t(p * all.size()))]; };
    return {all.size() / wall, sum / all.size(), pct(0.50), pct(0.95), pct(0.99)};
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fputs("usage: bench <index.idx> [numQueries] [threads]\n", stderr); return 2; }
    const size_t numQ = argc >= 3 ? std::strtoull(argv[2], nullptr, 10) : 20000;
    const unsigned threads = argc >= 4 ? std::atoi(argv[3]) : 4;

    std::string err;
    auto ix = Index::load(argv[1], &err);
    if (!ix) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }

    // Vocabulary sorted by document frequency (stable under the stemmer only).
    std::vector<std::pair<uint32_t, std::string>> vocab;
    for (auto& [term, ti] : ix->dictionary())
        if (ti.df >= 2 && term.size() >= 3 && stem(term) == term) vocab.push_back({ti.df, term});
    std::sort(vocab.begin(), vocab.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    if (vocab.empty()) { std::fputs("index has no usable terms\n", stderr); return 1; }

    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> uni(0, 1);
    auto term = [&] { double u = uni(rng); return vocab[size_t(u * u * vocab.size())].second; };

    // Pool of unique queries: 50% two-term AND, 20% three-term AND, 15% OR, 10% phrase, 5% with NOT.
    std::vector<std::string> pool;
    for (int i = 0; i < 5000; ++i) {
        double r = uni(rng);
        if (r < 0.50) pool.push_back(term() + " " + term());
        else if (r < 0.70) pool.push_back(term() + " " + term() + " " + term());
        else if (r < 0.85) pool.push_back(term() + " OR " + term());
        else if (r < 0.95) pool.push_back("\"" + term() + " " + term() + "\"");
        else pool.push_back(term() + " " + term() + " NOT " + term());
    }
    // Workload skewed toward popular queries (like real traffic), so caching matters.
    std::vector<std::string> work;
    for (size_t i = 0; i < numQ; ++i) { double u = uni(rng); work.push_back(pool[size_t(u * u * u * pool.size())]); }

    std::printf("index: %u docs, %zu terms, %.1f MB postings | %zu queries, %u threads\n\n", ix->numDocs(), ix->numTerms(),
                ix->postingsBytes() / 1e6, numQ, threads);
    std::printf("%-12s %10s %10s %10s %10s %10s\n", "mode", "QPS", "mean us", "p50 us", "p95 us", "p99 us");

    {   // cold: no cache, after a warm-up pass so the OS page cache is hot
        Engine eng(*ix, 0);
        runWorkload(eng, std::vector<std::string>(work.begin(), work.begin() + std::min<size_t>(2000, work.size())), threads);
        Stats s = runWorkload(eng, work, threads);
        std::printf("%-12s %10.0f %10.1f %10.1f %10.1f %10.1f\n", "no cache", s.qps, s.mean, s.p50, s.p95, s.p99);
    }
    {
        Engine eng(*ix, 4096);
        Stats s = runWorkload(eng, work, threads);
        std::printf("%-12s %10.0f %10.1f %10.1f %10.1f %10.1f\n", "LRU cache", s.qps, s.mean, s.p50, s.p95, s.p99);
        std::printf("\ncache hit rate: %.1f%%\n", 100.0 * eng.cacheHits() / std::max<uint64_t>(1, eng.cacheHits() + eng.cacheMisses()));
    }
    return 0;
}
