#pragma once
#include <cstdint>
#include <list>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace searchx {

// Thread-safe LRU cache.
template <class K, class V>
class LruCache {
public:
    explicit LruCache(size_t capacity) : cap_(capacity) {}

    bool get(const K& key, V& out) {
        if (cap_ == 0) return false;
        std::lock_guard<std::mutex> g(m_);
        auto it = map_.find(key);
        if (it == map_.end()) { ++misses_; return false; }
        list_.splice(list_.begin(), list_, it->second);
        out = it->second->second;
        ++hits_;
        return true;
    }

    void put(const K& key, V value) {
        if (cap_ == 0) return;
        std::lock_guard<std::mutex> g(m_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->second = std::move(value);
            list_.splice(list_.begin(), list_, it->second);
            return;
        }
        list_.emplace_front(key, std::move(value));
        map_[key] = list_.begin();
        if (map_.size() > cap_) {
            map_.erase(list_.back().first);
            list_.pop_back();
        }
    }

    uint64_t hits() const { std::lock_guard<std::mutex> g(m_); return hits_; }
    uint64_t misses() const { std::lock_guard<std::mutex> g(m_); return misses_; }

private:
    size_t cap_;
    mutable std::mutex m_;
    std::list<std::pair<K, V>> list_;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> map_;
    uint64_t hits_ = 0, misses_ = 0;
};

}  // namespace searchx
