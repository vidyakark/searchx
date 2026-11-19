#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "searchx/varbyte.hpp"

namespace searchx {

constexpr uint32_t kSkipInterval = 64;   // one skip entry per 64 postings
constexpr size_t kSkipEntryBytes = 12;   // {docId, byteOffset, prevDocId}

struct TermInfo {
    uint64_t offset = 0;  // into the postings blob
    uint32_t df = 0;      // document frequency
    uint32_t nskips = 0;  // number of skip entries stored before the postings
    uint32_t bytes = 0;   // encoded size of the postings
};

struct RawDoc {
    std::string title, text;
};

// Forward iterator over one term's compressed posting list, with skip-pointer advance().
// Posting layout: [docId delta][tf][positionBytes][position deltas...]  (all varbyte)
class PostingIter {
public:
    PostingIter(const uint8_t* base, const TermInfo& ti)
        : skips_(base),
          nskips_(ti.nskips),
          dataStart_(base + size_t(ti.nskips) * kSkipEntryBytes),
          p_(dataStart_),
          end_(dataStart_ + ti.bytes) {
        next();
    }

    bool valid() const { return valid_; }
    uint32_t doc() const { return doc_; }
    uint32_t tf() const { return tf_; }

    bool next() {
        if (p_ >= end_) { valid_ = false; return false; }
        doc_ = last_ + vbDecode(p_);
        last_ = doc_;
        tf_ = vbDecode(p_);
        posBytes_ = vbDecode(p_);
        pos_ = p_;
        p_ += posBytes_;
        ++idx_;
        valid_ = true;
        return true;
    }

    // Move to the first posting with doc >= target. Uses skip pointers to jump over blocks.
    bool advance(uint32_t target) {
        if (!valid_) return false;
        if (doc_ >= target) return true;
        if (nskips_ > 1) {
            uint32_t lo = 0, hi = nskips_;  // last skip entry whose docId <= target
            while (lo + 1 < hi) {
                uint32_t mid = (lo + hi) / 2;
                if (skipField(mid, 0) <= target) lo = mid; else hi = mid;
            }
            if (uint64_t(lo) * kSkipInterval >= idx_) {  // only jump forward
                p_ = dataStart_ + skipField(lo, 1);
                last_ = skipField(lo, 2);
                idx_ = lo * kSkipInterval;
                if (!next()) return false;
            }
        }
        while (doc_ < target) {
            if (!next()) return false;
        }
        return true;
    }

    // Decode the positions of the current posting.
    void positions(std::vector<uint32_t>& out) const {
        out.clear();
        const uint8_t* q = pos_;
        const uint8_t* e = pos_ + posBytes_;
        uint32_t cur = 0;
        while (q < e) { cur += vbDecode(q); out.push_back(cur); }
    }

private:
    uint32_t skipField(uint32_t k, int f) const {
        uint32_t v;
        std::memcpy(&v, skips_ + size_t(k) * kSkipEntryBytes + f * 4, 4);
        return v;
    }

    const uint8_t* skips_;
    uint32_t nskips_;
    const uint8_t* dataStart_;
    const uint8_t* p_;
    const uint8_t* end_;
    const uint8_t* pos_ = nullptr;
    uint32_t posBytes_ = 0;
    uint32_t last_ = 0, doc_ = 0, tf_ = 0, idx_ = 0;
    bool valid_ = false;
};

class Index {
public:
    ~Index();
    Index(const Index&) = delete;
    Index& operator=(const Index&) = delete;

    // Builds an index; tokenization runs on `threads` worker threads (0 = all cores).
    static std::unique_ptr<Index> build(const std::vector<RawDoc>& docs, unsigned threads = 0);
    // Memory-maps an index file written by save().
    static std::unique_ptr<Index> load(const std::string& path, std::string* err = nullptr);
    bool save(const std::string& path) const;

    uint32_t numDocs() const { return static_cast<uint32_t>(lens_.size()); }
    size_t numTerms() const { return dict_.size(); }
    uint32_t docLen(uint32_t d) const { return lens_[d]; }
    double avgDocLen() const { return lens_.empty() ? 0.0 : double(totalLen_) / lens_.size(); }
    uint64_t totalTokens() const { return totalLen_; }
    uint64_t totalPostings() const { return totalPostings_; }
    size_t postingsBytes() const { return postingsSize_; }
    const std::string& title(uint32_t d) const { return titles_[d]; }
    const std::string& snippet(uint32_t d) const { return snippets_[d]; }

    const TermInfo* find(const std::string& term) const {
        auto it = dict_.find(term);
        return it == dict_.end() ? nullptr : &it->second;
    }
    PostingIter iter(const TermInfo& ti) const { return PostingIter(postings_ + ti.offset, ti); }
    const std::unordered_map<std::string, TermInfo>& dictionary() const { return dict_; }

private:
    Index() = default;

    std::vector<uint32_t> lens_;
    std::vector<std::string> titles_, snippets_;
    std::unordered_map<std::string, TermInfo> dict_;
    uint64_t totalLen_ = 0, totalPostings_ = 0;

    const uint8_t* postings_ = nullptr;
    size_t postingsSize_ = 0;
    std::vector<uint8_t> owned_;  // used right after build()
    void* map_ = nullptr;         // used after load()
    size_t mapLen_ = 0;
};

}  // namespace searchx
