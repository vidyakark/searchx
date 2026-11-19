// Generates a synthetic corpus with a Zipfian vocabulary (like real text) for benchmarking.
//   gen_corpus <numDocs> <out.tsv> [seed=42]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) { std::fputs("usage: gen_corpus <numDocs> <out.tsv> [seed]\n", stderr); return 2; }
    const size_t numDocs = std::strtoull(argv[1], nullptr, 10);
    std::mt19937_64 rng(argc >= 4 ? std::strtoull(argv[3], nullptr, 10) : 42);

    // Pronounceable pseudo-words from syllables.
    const char* cons = "bdfgklmnprstvz";
    const char* vows = "aeiou";
    const size_t V = 50000;
    std::vector<std::string> vocab;
    vocab.reserve(V);
    std::uniform_int_distribution<int> nsyl(2, 4), ci(0, 13), vi(0, 4);
    while (vocab.size() < V) {
        std::string w;
        for (int s = nsyl(rng); s > 0; --s) { w += cons[ci(rng)]; w += vows[vi(rng)]; }
        w += "xqz"[rng() % 3];  // fixed ending keeps words stable under the stemmer
        vocab.push_back(w);
    }
    std::sort(vocab.begin(), vocab.end());
    vocab.erase(std::unique(vocab.begin(), vocab.end()), vocab.end());
    std::shuffle(vocab.begin(), vocab.end(), rng);

    // Zipf distribution over ranks.
    std::vector<double> cdf(vocab.size());
    double sum = 0;
    for (size_t r = 0; r < vocab.size(); ++r) cdf[r] = (sum += 1.0 / std::pow(r + 1, 1.05));
    std::uniform_real_distribution<double> uni(0.0, sum);
    auto pick = [&]() -> const std::string& {
        size_t i = std::upper_bound(cdf.begin(), cdf.end(), uni(rng)) - cdf.begin();
        return vocab[std::min(i, vocab.size() - 1)];
    };

    FILE* out = std::fopen(argv[2], "w");
    if (!out) { std::perror("open"); return 1; }
    std::uniform_int_distribution<int> len(60, 250), tlen(3, 6);
    std::string line;
    for (size_t d = 0; d < numDocs; ++d) {
        line.clear();
        for (int i = tlen(rng); i > 0; --i) { line += pick(); line += ' '; }
        line.back() = '\t';
        for (int i = len(rng); i > 0; --i) { line += pick(); line += ' '; }
        line.back() = '\n';
        std::fputs(line.c_str(), out);
    }
    std::fclose(out);
    std::printf("wrote %zu docs (vocab %zu) to %s\n", numDocs, vocab.size(), argv[2]);
    return 0;
}
