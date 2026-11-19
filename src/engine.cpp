#include "searchx/engine.hpp"

#include <chrono>

namespace searchx {

SearchResult Engine::search(const std::string& query, size_t k) const {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    auto micros = [&] { return std::chrono::duration<double, std::micro>(clock::now() - t0).count(); };

    const std::string key = std::to_string(k) + ':' + query;
    std::shared_ptr<const SearchResult> hit;
    if (cache_.get(key, hit)) {
        SearchResult r = *hit;
        r.cached = true;
        r.micros = micros();
        return r;
    }

    SearchResult r;
    if (NodePtr root = parseQuery(query)) {
        std::vector<uint32_t> cands = evaluate(ix_, *root);
        r.totalMatches = cands.size();
        r.hits = rankBM25(ix_, *root, cands, k);
    }
    r.micros = micros();
    cache_.put(key, std::make_shared<const SearchResult>(r));
    return r;
}

}  // namespace searchx
