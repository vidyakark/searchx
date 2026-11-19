#include "searchx/index.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <thread>

#include "searchx/tokenizer.hpp"

namespace searchx {
namespace {

// Per-thread, uncompressed postings for one batch of documents.
struct Plist {
    std::vector<uint32_t> docs, tfs, pos;
};

// Incremental encoder for one term's compressed posting list.
struct Enc {
    std::vector<uint8_t> bytes;
    std::vector<std::array<uint32_t, 3>> skips;  // {docId, byte offset, previous docId}
    uint32_t last = 0, count = 0;
};

void encAdd(Enc& e, uint32_t doc, uint32_t tf, const uint32_t* pos, std::vector<uint8_t>& tmp) {
    if (e.count % kSkipInterval == 0)
        e.skips.push_back({doc, static_cast<uint32_t>(e.bytes.size()), e.last});
    vbEncode(doc - e.last, e.bytes);  // delta-encode doc ids
    e.last = doc;
    vbEncode(tf, e.bytes);
    tmp.clear();
    uint32_t prev = 0;
    for (uint32_t j = 0; j < tf; ++j) { vbEncode(pos[j] - prev, tmp); prev = pos[j]; }
    vbEncode(static_cast<uint32_t>(tmp.size()), e.bytes);  // lets us skip positions quickly
    e.bytes.insert(e.bytes.end(), tmp.begin(), tmp.end());
    ++e.count;
}

std::string makeSnippet(const std::string& text) {
    std::string s;
    for (char c : text) {
        if (s.size() >= 160) break;
        s.push_back((c == '\n' || c == '\r' || c == '\t') ? ' ' : c);
    }
    if (text.size() > 160) {  // don't leave a half UTF-8 character at the cut
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
        if (!s.empty() && static_cast<unsigned char>(s.back()) >= 0xC0) s.pop_back();
        s += "...";
    }
    return s;
}

constexpr char kMagic[8] = {'S', 'R', 'C', 'H', 'X', '0', '0', '1'};
constexpr size_t kHeaderBytes = 48;

}  // namespace

Index::~Index() {
    if (map_) munmap(map_, mapLen_);
}

std::unique_ptr<Index> Index::build(const std::vector<RawDoc>& docs, unsigned threads) {
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
    std::unique_ptr<Index> idx(new Index());
    const size_t N = docs.size();
    idx->lens_.assign(N, 0);
    idx->titles_.resize(N);
    idx->snippets_.resize(N);

    std::unordered_map<std::string, Enc> encs;
    std::vector<uint8_t> tmp;
    const size_t kBatch = 32768;  // bounds memory: tokenize a batch in parallel, then merge in doc order

    for (size_t b0 = 0; b0 < N; b0 += kBatch) {
        const size_t b1 = std::min(N, b0 + kBatch);
        std::vector<std::unordered_map<std::string, Plist>> parts(threads);
        std::vector<std::thread> pool;
        const size_t per = (b1 - b0 + threads - 1) / threads;
        for (unsigned t = 0; t < threads; ++t) {
            const size_t s = b0 + t * per, e = std::min(b1, s + per);
            if (s >= e) break;
            pool.emplace_back([&, t, s, e] {
                std::vector<Token> toks;
                auto& part = parts[t];
                for (size_t d = s; d < e; ++d) {
                    tokenize(docs[d].text, toks);
                    idx->lens_[d] = static_cast<uint32_t>(toks.size());
                    idx->titles_[d] = docs[d].title;
                    idx->snippets_[d] = makeSnippet(docs[d].text);
                    std::stable_sort(toks.begin(), toks.end(),
                                     [](const Token& a, const Token& b) { return a.term < b.term; });
                    for (size_t i = 0; i < toks.size();) {
                        size_t j = i;
                        while (j < toks.size() && toks[j].term == toks[i].term) ++j;
                        Plist& pl = part[toks[i].term];
                        pl.docs.push_back(static_cast<uint32_t>(d));
                        pl.tfs.push_back(static_cast<uint32_t>(j - i));
                        for (size_t k = i; k < j; ++k) pl.pos.push_back(toks[k].pos);
                        i = j;
                    }
                }
            });
        }
        for (auto& th : pool) th.join();

        for (auto& part : parts) {  // thread order == doc order, so doc ids stay ascending
            for (auto& [term, pl] : part) {
                Enc& e = encs[term];
                size_t pi = 0;
                for (size_t i = 0; i < pl.docs.size(); ++i) {
                    encAdd(e, pl.docs[i], pl.tfs[i], pl.pos.data() + pi, tmp);
                    pi += pl.tfs[i];
                }
            }
            part.clear();
        }
    }

    for (uint32_t l : idx->lens_) idx->totalLen_ += l;

    std::vector<const std::string*> names;
    names.reserve(encs.size());
    for (auto& kv : encs) names.push_back(&kv.first);
    std::sort(names.begin(), names.end(), [](auto* a, auto* b) { return *a < *b; });

    std::vector<uint8_t>& blob = idx->owned_;
    for (const std::string* name : names) {
        Enc& e = encs[*name];
        TermInfo ti;
        ti.offset = blob.size();
        ti.df = e.count;
        ti.nskips = static_cast<uint32_t>(e.skips.size());
        ti.bytes = static_cast<uint32_t>(e.bytes.size());
        for (auto& s : e.skips) {
            uint8_t b[kSkipEntryBytes];
            std::memcpy(b, s.data(), kSkipEntryBytes);
            blob.insert(blob.end(), b, b + kSkipEntryBytes);
        }
        blob.insert(blob.end(), e.bytes.begin(), e.bytes.end());
        idx->dict_.emplace(*name, ti);
        idx->totalPostings_ += ti.df;
        std::vector<uint8_t>().swap(e.bytes);
    }
    idx->postings_ = blob.data();
    idx->postingsSize_ = blob.size();
    return idx;
}

bool Index::save(const std::string& path) const {
    std::vector<uint8_t> meta;
    auto put32 = [&](uint32_t v) { uint8_t b[4]; std::memcpy(b, &v, 4); meta.insert(meta.end(), b, b + 4); };
    auto put64 = [&](uint64_t v) { uint8_t b[8]; std::memcpy(b, &v, 8); meta.insert(meta.end(), b, b + 8); };
    auto putStr = [&](const std::string& s) {
        put32(static_cast<uint32_t>(s.size()));
        meta.insert(meta.end(), s.begin(), s.end());
    };
    for (size_t d = 0; d < lens_.size(); ++d) { put32(lens_[d]); putStr(titles_[d]); putStr(snippets_[d]); }
    for (auto& [term, ti] : dict_) {
        putStr(term);
        put64(ti.offset);
        put32(ti.df);
        put32(ti.nskips);
        put32(ti.bytes);
    }

    uint8_t header[kHeaderBytes];
    std::memcpy(header, kMagic, 8);
    uint64_t f[5] = {lens_.size(), dict_.size(), totalLen_, meta.size(), postingsSize_};
    std::memcpy(header + 8, f, sizeof f);

    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    bool ok = std::fwrite(header, 1, kHeaderBytes, fp) == kHeaderBytes &&
              std::fwrite(meta.data(), 1, meta.size(), fp) == meta.size() &&
              (postingsSize_ == 0 || std::fwrite(postings_, 1, postingsSize_, fp) == postingsSize_);
    return std::fclose(fp) == 0 && ok;
}

std::unique_ptr<Index> Index::load(const std::string& path, std::string* err) {
    auto fail = [&](const char* m) -> std::unique_ptr<Index> {
        if (err) *err = m;
        return nullptr;
    };
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return fail("cannot open index file");
    struct stat st;
    if (fstat(fd, &st) != 0 || static_cast<size_t>(st.st_size) < kHeaderBytes) { close(fd); return fail("index file too small"); }
    void* m = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return fail("mmap failed");

    std::unique_ptr<Index> idx(new Index());
    idx->map_ = m;
    idx->mapLen_ = st.st_size;
    const uint8_t* base = static_cast<const uint8_t*>(m);
    if (std::memcmp(base, kMagic, 8) != 0) return fail("bad magic (not a searchx index)");
    uint64_t f[5];
    std::memcpy(f, base + 8, sizeof f);
    const uint64_t numDocs = f[0], numTerms = f[1], metaSize = f[3], postSize = f[4];
    if (kHeaderBytes + metaSize + postSize != static_cast<uint64_t>(st.st_size)) return fail("corrupt index (size mismatch)");
    idx->totalLen_ = f[2];

    const uint8_t* p = base + kHeaderBytes;
    const uint8_t* end = p + metaSize;
    bool ok = true;
    auto u32 = [&]() -> uint32_t { if (p + 4 > end) { ok = false; return 0; } uint32_t v; std::memcpy(&v, p, 4); p += 4; return v; };
    auto u64 = [&]() -> uint64_t { if (p + 8 > end) { ok = false; return 0; } uint64_t v; std::memcpy(&v, p, 8); p += 8; return v; };
    auto str = [&]() -> std::string {
        uint32_t n = u32();
        if (!ok || p + n > end) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n;
        return s;
    };

    idx->lens_.resize(numDocs);
    idx->titles_.resize(numDocs);
    idx->snippets_.resize(numDocs);
    for (uint64_t d = 0; d < numDocs && ok; ++d) { idx->lens_[d] = u32(); idx->titles_[d] = str(); idx->snippets_[d] = str(); }
    idx->dict_.reserve(numTerms);
    for (uint64_t t = 0; t < numTerms && ok; ++t) {
        std::string term = str();
        TermInfo ti;
        ti.offset = u64(); ti.df = u32(); ti.nskips = u32(); ti.bytes = u32();
        if (ti.offset + size_t(ti.nskips) * kSkipEntryBytes + ti.bytes > postSize) ok = false;
        idx->totalPostings_ += ti.df;
        idx->dict_.emplace(std::move(term), ti);
    }
    if (!ok) return fail("corrupt index (bad metadata)");
    idx->postings_ = p;
    idx->postingsSize_ = postSize;
    return idx;
}

}  // namespace searchx
