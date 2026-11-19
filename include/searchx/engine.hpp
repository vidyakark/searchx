#pragma once
#include <memory>
#include <string>
#include <vector>

#include "searchx/index.hpp"
#include "searchx/lru.hpp"
#include "searchx/query.hpp"

namespace searchx {

struct SearchResult {
    std::vector<Hit> hits;
    uint64_t totalMatches = 0;
    double micros = 0;
    bool cached = false;
};

// Parse -> evaluate -> rank, with an LRU result cache. search() is thread-safe.
class Engine {
public:
    explicit Engine(const Index& ix, size_t cacheCapacity = 4096) : ix_(ix), cache_(cacheCapacity) {}

    SearchResult search(const std::string& query, size_t k = 10) const;
    const Index& index() const { return ix_; }
    uint64_t cacheHits() const { return cache_.hits(); }
    uint64_t cacheMisses() const { return cache_.misses(); }

private:
    const Index& ix_;
    mutable LruCache<std::string, std::shared_ptr<const SearchResult>> cache_;
};

}  // namespace searchx
