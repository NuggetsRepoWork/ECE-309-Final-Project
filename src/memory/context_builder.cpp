// src/memory/context_builder.cpp — YOU IMPLEMENT. Spec §4.4.

#include "memory/context_builder.h"
#include "util/min_heap.h"

#include <utility>

ContextPlan select_context(const std::vector<ContextItem>& items,
                           std::size_t budget, std::size_t keep_recent) {
    std::size_t n = items.size();

    std::size_t total = 0;
    for (std::size_t i = 0; i < n; i++) {
        total = total + items[i].tokens;
    }

    std::vector<bool> keep(n, true);

    std::size_t first_recent = 0;
    if (keep_recent < n) {
        first_recent = n - keep_recent;
    }

    MinHeap<std::pair<int, std::size_t>> heap;
    for (std::size_t i = 0; i < n; i++) {
        bool is_protected = false;
        if (items[i].role == Role::System) {
            is_protected = true;
        }
        if (items[i].pinned == true) {
            is_protected = true;
        }
        if (i >= first_recent) {
            is_protected = true;
        }
        if (is_protected == false) {
            heap.push(std::make_pair(drop_rank(items[i].role), i));
        }
    }

    while (total > budget && heap.empty() == false) {
        std::pair<int, std::size_t> next = heap.pop();
        keep[next.second] = false;
        total = total - items[next.second].tokens;
    }

    ContextPlan plan;
    for (std::size_t i = 0; i < n; i++) {
        if (keep[i] == true) {
            plan.keep.push_back(i);
        }
    }
    plan.total_tokens = total;
    plan.over_budget = (total > budget);
    return plan;
}
