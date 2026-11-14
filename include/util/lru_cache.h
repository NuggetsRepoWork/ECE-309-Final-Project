// include/util/lru_cache.h
//
// YOU IMPLEMENT (this whole file; it is a template). Spec §4.2.
//
// A fixed-capacity cache that evicts the Least Recently Used entry.
// Both get() and put() must be O(1) (expected), so it is built from two
// structures that point at the same nodes:
//
//   std::unordered_map<Key, Node*>   finds a key's node in O(1)
//   a doubly linked list of Nodes    keeps recency order:
//        head_ = most recently used ... tail_ = least recently used
//
// Rules for this file:
//   * Write the doubly linked list yourself (Node with prev/next pointers,
//     new/delete inside this class only). Do NOT use std::list, std::deque
//     or std::forward_list (don't even #include their headers), or a
//     std::vector you shift around (spec §7.3).
//   * std::unordered_map is allowed (and expected) for the key -> Node* index.
//   * No leaks, no double frees: ASan runs on every test.

#pragma once
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class LruCache {
public:
    struct Stats {
        std::size_t hits = 0;        // get() found the key
        std::size_t misses = 0;      // get() did not find the key
        std::size_t evictions = 0;   // put() removed the LRU entry to make room
    };

    // A cache holding at most `capacity` entries. Capacity 0 is legal: such a
    // cache never stores anything (every get() is a miss).
    explicit LruCache(std::size_t capacity) : capacity_(capacity) {}

    // Frees every node.
    ~LruCache() { clear(); }

    // Copying a cache of raw-pointer nodes is not needed; forbid it.
    LruCache(const LruCache&) = delete;
    LruCache& operator=(const LruCache&) = delete;

    // Hit: marks the entry most recently used, counts a hit, and returns a
    //      pointer to its value (valid until the next put/erase/clear).
    // Miss: counts a miss and returns nullptr.
    const Value* get(const Key& key) {
        auto it = map_.find(key);
        if (it == map_.end()) {
            stats_.misses = stats_.misses + 1;
            return nullptr;
        }
        stats_.hits = stats_.hits + 1;
        Node* node = it->second;
        unlink(node);
        push_front(node);
        return &node->value;
    }

    // Key present: replaces its value and marks it most recently used (no
    //   eviction, no hit/miss counted).
    // Key absent: if the cache is full, evicts the least recently used entry
    //   (counting one eviction), then inserts the new entry as most recently
    //   used. With capacity 0, does nothing.
    void put(const Key& key, Value value) {
        if (capacity_ == 0) {
            return;
        }
        auto it = map_.find(key);
        if (it != map_.end()) {
            Node* existing = it->second;
            existing->value = std::move(value);
            unlink(existing);
            push_front(existing);
            return;
        }
        if (map_.size() >= capacity_) {
            Node* victim = tail_;
            unlink(victim);
            map_.erase(victim->key);
            delete victim;
            stats_.evictions = stats_.evictions + 1;
        }
        Node* node = new Node{key, std::move(value), nullptr, nullptr};
        push_front(node);
        map_[key] = node;
    }

    // True if the key is cached. Does NOT change recency or stats.
    bool contains(const Key& key) const {
        auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }
        return true;
    }

    // Removes the key if present (not an eviction). Returns whether it was.
    bool erase(const Key& key) {
        auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }
        Node* node = it->second;
        unlink(node);
        map_.erase(it);
        delete node;
        return true;
    }

    // Removes every entry. Stats are kept.
    void clear() {
        Node* current = head_;
        while (current != nullptr) {
            Node* next = current->next;
            delete current;
            current = next;
        }
        head_ = nullptr;
        tail_ = nullptr;
        map_.clear();
    }

    std::size_t size() const noexcept { return map_.size(); }
    std::size_t capacity() const noexcept { return capacity_; }
    Stats       stats() const noexcept { return stats_; }

    // Keys from most to least recently used, by walking the list from head_.
    // Used by the tests to check your recency order.
    std::vector<Key> keys_mru_to_lru() const {
        std::vector<Key> out;
        for (Node* n = head_; n != nullptr; n = n->next) out.push_back(n->key);
        return out;
    }

private:
    struct Node {
        Key   key;
        Value value;
        Node* prev;
        Node* next;
    };

    // ===== YOUR HELPERS AND MEMBERS =====
    // Suggested helpers: unlink(Node*), push_front(Node*), move_to_front(Node*).
    // Clue for unlink(n): four cases. If n has a prev, prev->next skips n,
    // otherwise n was head_ and head_ moves to n->next. Likewise, if n has
    // a next, next->prev skips n, otherwise n was tail_ and tail_ moves to
    // n->prev. Unlinking the only node leaves head_ == tail_ == nullptr.
    // Spec Appendix B lists every pointer write.

    void unlink(Node* node) {
        if (node->prev != nullptr) {
            node->prev->next = node->next;
        }
        else {
            head_ = node->next;
        }
        if (node->next != nullptr) {
            node->next->prev = node->prev;
        }
        else {
            tail_ = node->prev;
        }
        node->prev = nullptr;
        node->next = nullptr;
    }

    void push_front(Node* node) {
        node->prev = nullptr;
        node->next = head_;
        if (head_ != nullptr) {
            head_->prev = node;
        }
        head_ = node;
        if (tail_ == nullptr) {
            tail_ = node;
        }
    }

    std::unordered_map<Key, Node*, Hash> map_;
    Node*       head_ = nullptr;
    Node*       tail_ = nullptr;
    std::size_t capacity_;
    Stats       stats_;
};