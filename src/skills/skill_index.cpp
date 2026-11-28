// src/skills/skill_index.cpp — YOU IMPLEMENT. Spec §6.3.

#include "skills/skill_index.h"
#include "util/min_heap.h"
#include "util/text.h"

#include <utility>

void SkillIndex::add(std::size_t doc_id, std::string_view text) {
    std::vector<std::string> words = unique_tokens(text);
    for (std::size_t i = 0; i < words.size(); i++) {
        postings_[words[i]].push_back(doc_id);
    }
}

const std::vector<std::size_t>* SkillIndex::postings(std::string_view word) const {
    auto it = postings_.find(std::string(word));
    if (it == postings_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::size_t SkillIndex::word_count() const noexcept {
    return postings_.size();
}

std::vector<std::size_t> SkillIndex::search(std::string_view query, std::size_t k) const {
    std::vector<std::size_t> result;
    if (k == 0) {
        return result;
    }

    std::vector<std::string> words = unique_tokens(query);
    std::unordered_map<std::size_t, std::size_t> score;
    for (std::size_t i = 0; i < words.size(); i++) {
        auto it = postings_.find(words[i]);
        if (it == postings_.end()) {
            continue;
        }
        for (std::size_t j = 0; j < it->second.size(); j++) {
            score[it->second[j]] = score[it->second[j]] + 1;
        }
    }

    std::vector<std::pair<std::size_t, std::size_t>> hits;
    for (auto it = score.begin(); it != score.end(); ++it) {
        hits.push_back(std::make_pair(it->second, it->first));
    }

    auto worse = [](const std::pair<std::size_t, std::size_t>& a,
                    const std::pair<std::size_t, std::size_t>& b) {
        if (a.first != b.first) {
            return a.first < b.first;
        }
        return a.second > b.second;
    };
    std::vector<std::pair<std::size_t, std::size_t>> best = top_k(hits, k, worse);

    for (std::size_t i = 0; i < best.size(); i++) {
        result.push_back(best[i].second);
    }
    return result;
}
