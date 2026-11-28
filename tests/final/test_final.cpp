// tests/final/test_final.cpp — YOUR TEST SUITE (spec §8).
//
// You need at least 20 tests. The 8 examples below are given and count:
//   Memory  (4): heap, lru, bst, ctx
//   Policy  (2): pol x2
//   Skills  (2): graph, index
// Write at least 12 more of your own, covering the list in spec §8.
// Name tests <area>_<what> with the areas heap, lru, bst, ctx, pol, graph,
// index, so the summary groups them.
//
//   ./build/test_final              run everything
//   ./build/test_final lru_         only tests whose names start with lru_
//   ./build/test_final =lru_evicts_least_recently_used    one test, exactly
//   ./build/test_final --list       list test names
//
// If a sanitizer error (ASan/UBSan) aborts the run, the tests after it do
// not run: rerun the failing test alone with =its_name and read the FIRST
// stack frame that is in your own file.
//
// CHECK_EQ(a, b) prints both values when it fails (vectors too), so prefer
// it over CHECK(a == b).
//
// Helpers you can use (tests/test_helpers.h): TempDir, write_text,
// write_skill, FixedPolicy, ScriptedConfirmer, LinesInput, StringOutput,
// parse_ok, args1. ManualClock is in policy/policy.h.

#include "../mini_test.h"
#include "../test_helpers.h"

#include "memory/context_builder.h"
#include "memory/memory_index.h"
#include "memory/memory_store.h"
#include "policy/policies.h"
#include "policy/policy_chain.h"
#include "skills/dep_graph.h"
#include "skills/skill_catalog.h"
#include "skills/skill_index.h"
#include "util/lru_cache.h"
#include "util/min_heap.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

// =========================================================================
// Memory — 4 examples
// =========================================================================

// Example 1 (heap): items come back smallest first, duplicates included.
TEST(heap_pops_in_order) {
    MinHeap<int> h;
    for (int x : {5, 1, 4, 1, 3}) h.push(x);
    CHECK_EQ(h.size(), std::size_t{5});
    CHECK_EQ(h.top(), 1);
    std::vector<int> got;
    while (!h.empty()) got.push_back(h.pop());
    CHECK_EQ(got, (std::vector<int>{1, 1, 3, 4, 5}));
}

// Example 2 (lru): the least recently USED entry is evicted, and get()
// counts as a use.
TEST(lru_evicts_least_recently_used) {
    LruCache<std::string, int> c(2);
    c.put("a", 1);
    c.put("b", 2);
    REQUIRE(c.get("a") != nullptr);   // "a" is now the most recent
    c.put("c", 3);                    // full: "b" must go
    CHECK(c.contains("a"));
    CHECK(!c.contains("b"));
    CHECK_EQ(c.keys_mru_to_lru(), (std::vector<std::string>{"c", "a"}));
    CHECK_EQ(c.stats().evictions, std::size_t{1});
}

// Example 3 (bst): removing a node with two children keeps the tree sorted,
// keeps every other record, and lowers size() by exactly one.
TEST(bst_remove_two_children) {
    MemoryIndex t;
    for (std::uint64_t id : {50, 30, 70, 60, 80}) REQUIRE(t.insert(MemoryRecord{id, "m" + std::to_string(id)}));
    REQUIRE(t.remove(70));
    std::vector<std::uint64_t> ids;
    t.for_each_in_order([&](const MemoryRecord& r) { ids.push_back(r.id); });
    CHECK_EQ(ids, (std::vector<std::uint64_t>{30, 50, 60, 80}));
    CHECK(t.find(70) == nullptr);
    REQUIRE(t.find(80) != nullptr);
    CHECK_EQ(t.find(80)->text, std::string("m80"));
    CHECK_EQ(t.size(), std::size_t{4});
}

// Example 4 (ctx): the worked example in spec §4.4.
TEST(ctx_spec_example) {
    std::vector<ContextItem> items{
        {Role::System, 10, false},     // 0  protected
        {Role::User, 10, false},       // 1
        {Role::Assistant, 10, false},  // 2  dropped 3rd (oldest assistant)
        {Role::Tool, 10, false},       // 3  dropped 1st
        {Role::Assistant, 10, false},  // 4
        {Role::Tool, 10, false},       // 5  dropped 2nd
        {Role::User, 10, false},       // 6  recent: protected
        {Role::Assistant, 10, false}}; // 7  recent: protected
    ContextPlan p = select_context(items, 50, 2);
    CHECK_EQ(p.keep, (std::vector<std::size_t>{0, 1, 4, 6, 7}));
    CHECK_EQ(p.total_tokens, std::size_t{50});
    CHECK(!p.over_budget);
}

// =========================================================================
// Policy — 2 examples
// =========================================================================

// Example 5 (pol): the sliding window. A request exactly `window` seconds
// after an allowed one no longer counts it.
TEST(pol_rate_limit_window_boundary) {
    ManualClock clock(100);
    RateLimitPolicy p(2, 10.0, clock);   // at most 2 per 10 s
    CommandRequest req;
    req.program = "ls";
    CHECK(p.evaluate(req).kind == Decision::Kind::Allow);   // t = 100
    clock.advance(1);
    CHECK(p.evaluate(req).kind == Decision::Kind::Allow);   // t = 101
    clock.advance(1);
    CHECK(p.evaluate(req).kind == Decision::Kind::Deny);    // t = 102: two in the window
    clock.advance(8);
    CHECK(p.evaluate(req).kind == Decision::Kind::Allow);   // t = 110: 100 has left (110 - 100 = 10)
    CHECK(p.tracked() <= 2u);
}

// Example 6 (pol): first Deny wins and later policies are not asked; with no
// Deny, the FIRST Ask is returned unchanged.
TEST(pol_chain_first_deny_stops_first_ask_wins) {
    auto ask1 = std::make_unique<FixedPolicy>(Decision::Kind::Ask, "ask1");
    auto ask2 = std::make_unique<FixedPolicy>(Decision::Kind::Ask, "ask2");
    PolicyChain chain;
    REQUIRE(chain.add(std::make_unique<FixedPolicy>(Decision::Kind::Allow, "ok")));
    REQUIRE(chain.add(std::move(ask1)));
    REQUIRE(chain.add(std::move(ask2)));
    CommandRequest req;
    req.program = "git";
    Decision d = chain.evaluate(req);
    CHECK(d.kind == Decision::Kind::Ask);
    CHECK_EQ(d.policy, std::string("ask1"));

    auto deny = std::make_unique<FixedPolicy>(Decision::Kind::Deny, "no");
    auto after = std::make_unique<FixedPolicy>(Decision::Kind::Allow, "after");
    FixedPolicy* after_ptr = after.get();   // still valid: the chain owns it now
    REQUIRE(chain.add(std::move(deny)));
    REQUIRE(chain.add(std::move(after)));
    d = chain.evaluate(req);
    CHECK(d.kind == Decision::Kind::Deny);
    CHECK_EQ(d.policy, std::string("no"));
    CHECK_EQ(after_ptr->calls, 0);     // never evaluated: the chain stopped at the Deny
}

// =========================================================================
// Skills — 2 examples
// =========================================================================

// Example 7 (graph): post-order load order, and a cycle message that lists
// only the nodes still on the DFS path (b is already finished).
TEST(graph_order_and_cycle_message) {
    DepGraph g;
    for (const char* n : {"a", "b", "c", "d"}) g.add_node(n);
    g.add_edge("a", "b");   // a requires b
    g.add_edge("a", "c");
    g.add_edge("b", "d");
    g.add_edge("c", "d");   // diamond: d is reached twice, but it is not a cycle
    std::vector<std::string> order;
    std::string error;
    REQUIRE(g.order_from("a", &order, &error));
    CHECK_EQ(order, (std::vector<std::string>{"d", "b", "c", "a"}));

    g.add_edge("c", "a");   // now a -> c -> a is a cycle
    CHECK(!g.order_from("a", &order, &error));
    CHECK_EQ(error, std::string("cycle: a -> c -> a"));
}

// Example 8 (index): rank by number of distinct query words; ties go to the
// smaller doc id.
TEST(index_ranking_and_ties) {
    SkillIndex ix;
    ix.add(0, "format numbers");
    ix.add(1, "csv report with table and numbers");
    ix.add(2, "table format");
    ix.add(3, "unrelated");
    CHECK_EQ(ix.search("csv table numbers", 10), (std::vector<std::size_t>{1, 0, 2}));
    CHECK_EQ(ix.search("format table", 1), (std::vector<std::size_t>{2}));
    REQUIRE(ix.postings("numbers") != nullptr);
    CHECK_EQ(*ix.postings("numbers"), (std::vector<std::size_t>{0, 1}));
}

// =========================================================================
// Your tests (at least 12 more). Spec §8 lists what they must cover.
// =========================================================================


// checks if top and pop throw when the heap is empty
TEST(heap_empty_throws) {
    MinHeap<int> h;
    CHECK_THROWS(h.top(), std::out_of_range);
    CHECK_THROWS(h.pop(), std::out_of_range);
    h.push(5);
    h.pop();
    CHECK_THROWS(h.pop(), std::out_of_range);
}

// checks if top_k works when k is 0 and when k is bigger than the list
TEST(heap_topk_k_zero_and_too_big) {
    std::vector<int> v;
    v.push_back(4);
    v.push_back(1);
    v.push_back(9);
    v.push_back(7);
    v.push_back(3);
    std::vector<int> r0 = top_k(v, 0);
    CHECK_EQ(r0.size(), std::size_t{ 0 });
    std::vector<int> r2 = top_k(v, 2);
    CHECK_EQ(r2, (std::vector<int>{9, 7}));
    std::vector<int> r10 = top_k(v, 10);
    CHECK_EQ(r10, (std::vector<int>{9, 7, 4, 3, 1}));
}

// checks if top_k gives the same answer as sorting everything
TEST(heap_topk_matches_sort) {
    std::mt19937 rng(42);
    std::vector<int> nums;
    for (int i = 0; i < 1000; i++) {
        nums.push_back(rng() % 100);   
    }
    std::vector<int> sorted = nums;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    int ks[] = { 1, 5, 10, 50, 1000 };
    for (int k : ks) {
        std::vector<int> got = top_k(nums, k);
        std::vector<int> want(sorted.begin(), sorted.begin() + k);
        CHECK_EQ(got, want);
    }
}
// prints how long top_k takes vs std::sort
TEST(heap_timing_vs_sort) {
    std::mt19937 rng(1);
    std::vector<int> nums;
    for (int i = 0; i < 100000; i++) {
        nums.push_back(rng());
    }
    auto start = std::chrono::steady_clock::now();
    std::vector<int> best = top_k(nums, 10);
    auto mid = std::chrono::steady_clock::now();
    std::vector<int> copy = nums;
    std::sort(copy.begin(), copy.end(), std::greater<int>());
    auto end = std::chrono::steady_clock::now();
    double ms1 = std::chrono::duration<double, std::milli>(mid - start).count();
    double ms2 = std::chrono::duration<double, std::milli>(end - mid).count();
    std::printf("    top_k (k=10): %.3f ms\n", ms1);
    std::printf("    std::sort:    %.3f ms\n", ms2);
    CHECK_EQ(best.size(), std::size_t{ 10 });
    CHECK_EQ(best[0], copy[0]);
}

// checks if updating a key, get, and the hit/miss/eviction counts work
TEST(lru_update_refresh_and_counts) {
    LruCache<std::string, int> c(3);
    c.put("a", 1);
    c.put("b", 2);
    c.put("c", 3);
    c.put("a", 10);
    CHECK_EQ(c.size(), std::size_t{3});
    CHECK_EQ(c.stats().evictions, std::size_t{0});
    const int* v = c.get("a");
    REQUIRE(v != nullptr);
    CHECK_EQ(*v, 10);
    CHECK_EQ(c.keys_mru_to_lru(), (std::vector<std::string>{"a", "c", "b"}));
    CHECK(c.get("nope") == nullptr);
    c.put("d", 4);
    CHECK(!c.contains("b"));
    CHECK_EQ(c.keys_mru_to_lru(), (std::vector<std::string>{"d", "a", "c"}));
    CHECK_EQ(c.stats().hits, std::size_t{1});
    CHECK_EQ(c.stats().misses, std::size_t{1});
    CHECK_EQ(c.stats().evictions, std::size_t{1});
}


// checks if capacity 0 and 1 work and erase works on the head, middle and tail
TEST(lru_capacity_and_erase) {
    LruCache<std::string, int> zero(0);
    zero.put("a", 1);
    CHECK_EQ(zero.size(), std::size_t{0});
    CHECK(zero.get("a") == nullptr);
    LruCache<std::string, int> one(1);
    one.put("a", 1);
    one.put("b", 2);
    CHECK(!one.contains("a"));
    CHECK_EQ(one.keys_mru_to_lru(), (std::vector<std::string>{"b"}));
    CHECK_EQ(one.stats().evictions, std::size_t{1});
    LruCache<std::string, int> c(4);
    c.put("a", 1);
    c.put("b", 2);
    c.put("c", 3);
    c.put("d", 4);
    CHECK(c.erase("d"));
    CHECK_EQ(c.keys_mru_to_lru(), (std::vector<std::string>{"c", "b", "a"}));
    CHECK(c.erase("b"));
    CHECK_EQ(c.keys_mru_to_lru(), (std::vector<std::string>{"c", "a"}));
    CHECK(c.erase("a"));
    CHECK_EQ(c.keys_mru_to_lru(), (std::vector<std::string>{"c"}));
    CHECK(!c.erase("zzz"));
    CHECK(c.erase("c"));
    CHECK_EQ(c.size(), std::size_t{0});
    c.put("x", 9);
    CHECK_EQ(c.keys_mru_to_lru(), (std::vector<std::string>{"x"}));
}

// checks if insert, find, in-order and height work
TEST(bst_insert_find_order_and_height) {
    MemoryIndex t;
    CHECK_EQ(t.height(), std::size_t{0});
    CHECK(t.find(5) == nullptr);
    REQUIRE(t.insert(MemoryRecord{50, "fifty"}));
    CHECK_EQ(t.height(), std::size_t{1});
    REQUIRE(t.insert(MemoryRecord{30, "thirty"}));
    REQUIRE(t.insert(MemoryRecord{70, "seventy"}));
    CHECK_EQ(t.height(), std::size_t{2});
    REQUIRE(t.insert(MemoryRecord{20, "twenty"}));
    CHECK_EQ(t.height(), std::size_t{3});
    CHECK(!t.insert(MemoryRecord{30, "other"}));
    CHECK_EQ(t.size(), std::size_t{4});
    REQUIRE(t.find(30) != nullptr);
    CHECK_EQ(t.find(30)->text, std::string("thirty"));
    CHECK(t.find(99) == nullptr);
    std::vector<std::uint64_t> ids;
    t.for_each_in_order([&ids](const MemoryRecord& r) { ids.push_back(r.id); });
    CHECK_EQ(ids, (std::vector<std::uint64_t>{20, 30, 50, 70}));
}

// checks if clear empties the tree and it can be used again
TEST(bst_clear_and_reuse) {
    MemoryIndex t;
    for (std::uint64_t i = 1; i <= 10; i++) {
        t.insert(MemoryRecord{i, "x"});
    }
    CHECK_EQ(t.size(), std::size_t{10});
    t.clear();
    CHECK_EQ(t.size(), std::size_t{0});
    CHECK_EQ(t.height(), std::size_t{0});
    CHECK(t.find(3) == nullptr);
    REQUIRE(t.insert(MemoryRecord{3, "again"}));
    CHECK_EQ(t.size(), std::size_t{1});
    t.clear();
    t.clear();
}
// checks if remove works on a leaf, a node with one child and the root
TEST(bst_remove_leaf_one_child_and_root) {
    MemoryIndex t;
    std::uint64_t ids[] = {50, 30, 70, 20, 40, 60, 80};
    for (std::uint64_t id : ids) {
        REQUIRE(t.insert(MemoryRecord{id, "m"}));
    }
    auto get_ids = [&t]() {
        std::vector<std::uint64_t> v;
        t.for_each_in_order([&v](const MemoryRecord& r) { v.push_back(r.id); });
        return v;
    };
    REQUIRE(t.remove(20));
    CHECK_EQ(get_ids(), (std::vector<std::uint64_t>{30, 40, 50, 60, 70, 80}));
    REQUIRE(t.remove(30));
    CHECK_EQ(get_ids(), (std::vector<std::uint64_t>{40, 50, 60, 70, 80}));
    REQUIRE(t.remove(50));
    CHECK_EQ(get_ids(), (std::vector<std::uint64_t>{40, 60, 70, 80}));
    CHECK(t.find(50) == nullptr);
    CHECK(!t.remove(999));
    CHECK_EQ(t.size(), std::size_t{4});
    REQUIRE(t.remove(40));
    REQUIRE(t.remove(60));
    REQUIRE(t.remove(70));
    REQUIRE(t.remove(80));
    CHECK_EQ(t.size(), std::size_t{0});
    CHECK_EQ(t.height(), std::size_t{0});
    REQUIRE(t.insert(MemoryRecord{1, "a"}));
    REQUIRE(t.insert(MemoryRecord{2, "b"}));
    REQUIRE(t.remove(1));
    CHECK(t.find(2) != nullptr);
    CHECK_EQ(t.size(), std::size_t{1});
}

// prints the height for in order vs shuffled inserts
TEST(bst_heights_sequential_vs_shuffled) {
    MemoryIndex seq;
    for (std::uint64_t i = 1; i <= 1000; i++) {
        seq.insert(MemoryRecord{i, "x"});
    }
    std::vector<std::uint64_t> order;
    for (std::uint64_t i = 1; i <= 1000; i++) {
        order.push_back(i);
    }
    std::mt19937 rng(5);
    std::shuffle(order.begin(), order.end(), rng);
    MemoryIndex shuf;
    for (std::uint64_t id : order) {
        shuf.insert(MemoryRecord{id, "x"});
    }
    std::printf("    height after 1..1000 in order:  %zu\n", seq.height());
    std::printf("    height after 1..1000 shuffled:  %zu\n", shuf.height());
    CHECK_EQ(seq.height(), std::size_t{1000});
    CHECK(shuf.height() < 100);
    CHECK_EQ(shuf.size(), std::size_t{1000});
}

// checks if protected items stay and the drop order and over_budget are right
TEST(ctx_protected_drop_order_and_over_budget) {
    std::vector<ContextItem> items{
        {Role::System, 10, false},
        {Role::Tool, 10, false},
        {Role::User, 10, true},
        {Role::Assistant, 10, false},
        {Role::Tool, 10, false},
        {Role::User, 10, false}};
    ContextPlan p = select_context(items, 25, 1);
    CHECK_EQ(p.keep, (std::vector<std::size_t>{0, 2, 5}));
    CHECK_EQ(p.total_tokens, std::size_t{30});
    CHECK(p.over_budget);
    ContextPlan all = select_context(items, 100, 1);
    CHECK_EQ(all.keep.size(), std::size_t{6});
    CHECK(!all.over_budget);
    std::vector<ContextItem> three{
        {Role::User, 10, false},
        {Role::Assistant, 10, false},
        {Role::Tool, 10, false}};
    CHECK_EQ(select_context(three, 20, 0).keep, (std::vector<std::size_t>{0, 1}));
    CHECK_EQ(select_context(three, 10, 0).keep, (std::vector<std::size_t>{0}));
}


// checks if empty input, a huge keep_recent and tiny budgets work
TEST(ctx_edge_cases) {
    std::vector<ContextItem> none;
    ContextPlan e = select_context(none, 10, 2);
    CHECK_EQ(e.keep.size(), std::size_t{0});
    CHECK_EQ(e.total_tokens, std::size_t{0});
    CHECK(!e.over_budget);
    std::vector<ContextItem> three{
        {Role::User, 10, false},
        {Role::Tool, 10, false},
        {Role::Assistant, 10, false}};
    ContextPlan big = select_context(three, 5, 10);
    CHECK_EQ(big.keep, (std::vector<std::size_t>{0, 1, 2}));
    CHECK_EQ(big.total_tokens, std::size_t{30});
    CHECK(big.over_budget);
    ContextPlan exact = select_context(three, 30, 0);
    CHECK_EQ(exact.keep.size(), std::size_t{3});
    CHECK(!exact.over_budget);
    std::vector<ContextItem> only_protected{
        {Role::System, 10, false},
        {Role::User, 10, true}};
    ContextPlan z = select_context(only_protected, 0, 0);
    CHECK_EQ(z.keep, (std::vector<std::size_t>{0, 1}));
    CHECK(z.over_budget);
    ContextPlan drop_all = select_context(three, 0, 0);
    CHECK_EQ(drop_all.keep.size(), std::size_t{0});
    CHECK_EQ(drop_all.total_tokens, std::size_t{0});
    CHECK(!drop_all.over_budget);
}

// checks if allowlist, patterns and confirm give the right allow/deny/ask
TEST(pol_allowlist_patterns_and_confirm) {
    auto make = [](const std::string& prog, std::vector<std::string> args) {
        CommandRequest r;
        r.program = prog;
        r.args = args;
        return r;
    };
    std::vector<std::string> ok_programs{"ls", "cat", "git"};
    AllowlistPolicy allow(ok_programs);
    CHECK(allow.evaluate(make("ls", {})).kind == Decision::Kind::Allow);
    CHECK(allow.evaluate(make("git", {"status"})).kind == Decision::Kind::Allow);
    CHECK(allow.evaluate(make("rm", {})).kind == Decision::Kind::Deny);
    CHECK(allow.evaluate(make("LS", {})).kind == Decision::Kind::Deny);
    CHECK(allow.evaluate(make("l", {})).kind == Decision::Kind::Deny);
    CHECK_EQ(allow.evaluate(make("ls", {})).policy, std::string("allowlist"));
    CHECK(!allow.evaluate(make("rm", {})).reason.empty());
    AllowlistPolicy nothing(std::vector<std::string>{});
    CHECK(nothing.evaluate(make("ls", {})).kind == Decision::Kind::Deny);
    std::vector<std::string> bad{";", "rm -rf", ""};
    ArgumentPatternPolicy pat(bad);
    CHECK(pat.evaluate(make("cat", {"a;", "rm", "b"})).kind == Decision::Kind::Deny);
    CHECK(pat.evaluate(make("rm", {"-rf", "/"})).kind == Decision::Kind::Deny);
    CHECK(pat.evaluate(make("ls", {"-l"})).kind == Decision::Kind::Allow);
    CHECK(pat.evaluate(make("ls", {})).kind == Decision::Kind::Allow);
    CHECK_EQ(pat.evaluate(make("rm", {"-rf"})).policy, std::string("patterns"));
    CHECK(!pat.evaluate(make("rm", {"-rf"})).reason.empty());
    ArgumentPatternPolicy no_patterns(std::vector<std::string>{});
    CHECK(no_patterns.evaluate(make("rm", {"-rf", "/"})).kind == Decision::Kind::Allow);
    std::vector<std::string> ask_programs{"git", "curl"};
    ConfirmPolicy conf(ask_programs);
    CHECK(conf.evaluate(make("git", {"status"})).kind == Decision::Kind::Ask);
    CHECK(conf.evaluate(make("curl", {})).kind == Decision::Kind::Ask);
    CHECK(conf.evaluate(make("ls", {})).kind == Decision::Kind::Allow);
    CHECK_EQ(conf.evaluate(make("git", {})).policy, std::string("confirm"));
    CHECK(!conf.evaluate(make("ls", {})).reason.empty());
}

// checks if the rate limit works with max 0, max 1 and lots of requests
TEST(pol_rate_limit_edges) {
    CommandRequest req;
    req.program = "ls";
    ManualClock c0(0);
    RateLimitPolicy zero(0, 10.0, c0);
    CHECK(zero.evaluate(req).kind == Decision::Kind::Deny);
    CHECK_EQ(zero.tracked(), std::size_t{0});
    ManualClock c1(50);
    RateLimitPolicy one(1, 5.0, c1);
    CHECK(one.evaluate(req).kind == Decision::Kind::Allow);
    c1.advance(4);
    CHECK(one.evaluate(req).kind == Decision::Kind::Deny);
    CHECK_EQ(one.tracked(), std::size_t{1});
    c1.advance(1);
    CHECK(one.evaluate(req).kind == Decision::Kind::Allow);
    CHECK_EQ(one.evaluate(req).policy, std::string("ratelimit"));
    CHECK(!one.evaluate(req).reason.empty());
    ManualClock c3(0);
    RateLimitPolicy three(3, 10.0, c3);
    for (int i = 0; i < 3; i++) {
        CHECK(three.evaluate(req).kind == Decision::Kind::Allow);
    }
    for (int i = 0; i < 20; i++) {
        CHECK(three.evaluate(req).kind == Decision::Kind::Deny);
    }
    CHECK_EQ(three.tracked(), std::size_t{3});
    c3.advance(10);
    CHECK(three.evaluate(req).kind == Decision::Kind::Allow);
    CHECK_EQ(three.tracked(), std::size_t{1});
    ManualClock c4(0);
    RateLimitPolicy busy(5, 3.0, c4);
    for (int i = 0; i < 1000; i++) {
        busy.evaluate(req);
        c4.advance(1);
        CHECK(busy.tracked() <= 5u);
    }
}
// checks if an empty chain denies, null is rejected and nested chains work
TEST(pol_chain_empty_null_and_nested) {
    auto make = [](const std::string& prog, std::vector<std::string> args) {
        CommandRequest r;
        r.program = prog;
        r.args = args;
        return r;
    };
    PolicyChain empty;
    CHECK_EQ(empty.size(), std::size_t{0});
    Decision d0 = empty.evaluate(make("ls", {}));
    CHECK(d0.kind == Decision::Kind::Deny);
    CHECK_EQ(d0.policy, std::string("chain"));
    CHECK(!d0.reason.empty());
    PolicyChain c;
    CHECK(!c.add(nullptr));
    CHECK_EQ(c.size(), std::size_t{0});
    std::vector<std::string> inner_ok{"ls", "git"};
    std::vector<std::string> inner_bad{";"};
    std::vector<std::string> ask_programs{"git"};
    auto inner = std::make_unique<PolicyChain>();
    REQUIRE(inner->add(std::make_unique<AllowlistPolicy>(inner_ok)));
    REQUIRE(inner->add(std::make_unique<ArgumentPatternPolicy>(inner_bad)));
    PolicyChain outer;
    REQUIRE(outer.add(std::move(inner)));
    REQUIRE(outer.add(std::make_unique<ConfirmPolicy>(ask_programs)));
    CHECK_EQ(outer.size(), std::size_t{2});
    Decision a = outer.evaluate(make("ls", {}));
    CHECK(a.kind == Decision::Kind::Allow);
    CHECK_EQ(a.policy, std::string("chain"));
    CHECK(!a.reason.empty());
    Decision b = outer.evaluate(make("git", {"status"}));
    CHECK(b.kind == Decision::Kind::Ask);
    CHECK_EQ(b.policy, std::string("confirm"));
    Decision e = outer.evaluate(make("rm", {"x"}));
    CHECK(e.kind == Decision::Kind::Deny);
    CHECK_EQ(e.policy, std::string("allowlist"));
    Decision f = outer.evaluate(make("ls", {";"}));
    CHECK(f.kind == Decision::Kind::Deny);
    CHECK_EQ(f.policy, std::string("patterns"));
}

// checks if deny beats ask in a chain and the rate limit works inside one
TEST(pol_chain_ask_vs_deny_and_rate_limit) {
    CommandRequest req;
    req.program = "make";
    std::vector<std::string> ask_programs{"make"};
    std::vector<std::string> allow_programs{"make"};
    std::vector<std::string> bad{"clean"};
    req.args = {"clean"};
    PolicyChain ask_then_deny;
    REQUIRE(ask_then_deny.add(std::make_unique<ConfirmPolicy>(ask_programs)));
    REQUIRE(ask_then_deny.add(std::make_unique<ArgumentPatternPolicy>(bad)));
    Decision d = ask_then_deny.evaluate(req);
    CHECK(d.kind == Decision::Kind::Deny);
    CHECK_EQ(d.policy, std::string("patterns"));
    req.args = {"all"};
    Decision a = ask_then_deny.evaluate(req);
    CHECK(a.kind == Decision::Kind::Ask);
    CHECK_EQ(a.policy, std::string("confirm"));
    ManualClock clock(0);
    PolicyChain limited;
    REQUIRE(limited.add(std::make_unique<AllowlistPolicy>(allow_programs)));
    REQUIRE(limited.add(std::make_unique<RateLimitPolicy>(2, 10.0, clock)));
    CHECK(limited.evaluate(req).kind == Decision::Kind::Allow);
    CHECK(limited.evaluate(req).kind == Decision::Kind::Allow);
    Decision blocked = limited.evaluate(req);
    CHECK(blocked.kind == Decision::Kind::Deny);
    CHECK_EQ(blocked.policy, std::string("ratelimit"));
    clock.advance(10);
    CHECK(limited.evaluate(req).kind == Decision::Kind::Allow);
}


// checks if add_node/add_edge work and a chain and diamond load in the right order
TEST(graph_chain_diamond_and_basics) {
    DepGraph g;
    CHECK_EQ(g.node_count(), std::size_t{0});
    CHECK(g.add_node("a"));
    CHECK(!g.add_node("a"));
    CHECK(g.has_node("a"));
    CHECK(!g.has_node("b"));
    CHECK(!g.add_edge("nope", "a"));
    CHECK(g.requirements("nope").empty());
    g.add_node("b");
    g.add_node("c");
    CHECK(g.add_edge("a", "c"));
    CHECK(g.add_edge("a", "b"));
    CHECK(!g.add_edge("a", "b"));
    CHECK_EQ(g.requirements("a"), (std::vector<std::string>{"c", "b"}));
    CHECK_EQ(g.node_count(), std::size_t{3});
    DepGraph chain;
    for (const char* n : {"a", "b", "c"}) chain.add_node(n);
    chain.add_edge("a", "b");
    chain.add_edge("b", "c");
    std::vector<std::string> order;
    std::string err;
    REQUIRE(chain.order_from("a", &order, &err));
    CHECK_EQ(order, (std::vector<std::string>{"c", "b", "a"}));
    REQUIRE(chain.order_from("b", &order, &err));
    CHECK_EQ(order, (std::vector<std::string>{"c", "b"}));
    REQUIRE(chain.order_from("c", &order, &err));
    CHECK_EQ(order, (std::vector<std::string>{"c"}));
    DepGraph d;
    for (const char* n : {"a", "b", "c", "d", "e"}) d.add_node(n);
    d.add_edge("a", "b");
    d.add_edge("a", "c");
    d.add_edge("b", "d");
    d.add_edge("c", "d");
    d.add_edge("d", "e");
    d.add_edge("c", "e");
    REQUIRE(d.order_from("a", &order, &err));
    CHECK_EQ(order, (std::vector<std::string>{"e", "d", "b", "c", "a"}));
    CHECK_EQ(order.size(), std::size_t{5});
}

// checks if the cycle, missing and unknown errors are right and a 5000 chain works
TEST(graph_cycles_missing_unknown_and_long_chain) {
    std::vector<std::string> order;
    std::string err;
    DepGraph self;
    self.add_node("a");
    self.add_edge("a", "a");
    CHECK(!self.order_from("a", &order, &err));
    CHECK_EQ(err, std::string("cycle: a -> a"));
    DepGraph three;
    for (const char* n : {"a", "b", "c"}) three.add_node(n);
    three.add_edge("a", "b");
    three.add_edge("b", "c");
    three.add_edge("c", "a");
    CHECK(!three.order_from("a", &order, &err));
    CHECK_EQ(err, std::string("cycle: a -> b -> c -> a"));
    DepGraph off_root;
    for (const char* n : {"a", "b", "c"}) off_root.add_node(n);
    off_root.add_edge("a", "b");
    off_root.add_edge("b", "c");
    off_root.add_edge("c", "b");
    CHECK(!off_root.order_from("a", &order, &err));
    CHECK_EQ(err, std::string("cycle: b -> c -> b"));
    DepGraph missing;
    for (const char* n : {"a", "b"}) missing.add_node(n);
    missing.add_edge("a", "b");
    missing.add_edge("b", "gone");
    CHECK(!missing.order_from("a", &order, &err));
    CHECK_EQ(err, std::string("missing: gone (required by b)"));
    CHECK(!missing.order_from("zzz", &order, &err));
    CHECK_EQ(err, std::string("unknown: zzz"));
    DepGraph first;
    for (const char* n : {"a", "b", "c"}) first.add_node(n);
    first.add_edge("a", "b");
    first.add_edge("a", "c");
    first.add_edge("b", "gone");
    first.add_edge("c", "a");
    CHECK(!first.order_from("a", &order, &err));
    CHECK_EQ(err, std::string("missing: gone (required by b)"));
    DepGraph big;
    const int n = 5000;
    for (int i = 0; i < n; i++) {
        big.add_node("s" + std::to_string(i));
    }
    for (int i = 0; i + 1 < n; i++) {
        big.add_edge("s" + std::to_string(i), "s" + std::to_string(i + 1));
    }
    REQUIRE(big.order_from("s0", &order, &err));
    CHECK_EQ(order.size(), std::size_t{5000});
    CHECK_EQ(order.front(), std::string("s4999"));
    CHECK_EQ(order.back(), std::string("s0"));
}

// checks if dependents come out nearest first and then by name
TEST(graph_dependents_order_cycles_and_missing) {
    DepGraph g;
    for (const char* n : {"base", "zed", "mid", "top", "aaa"}) g.add_node(n);
    g.add_edge("zed", "base");
    g.add_edge("mid", "base");
    g.add_edge("top", "mid");
    g.add_edge("aaa", "top");
    CHECK_EQ(g.dependents("base"), (std::vector<std::string>{"mid", "zed", "top", "aaa"}));
    CHECK_EQ(g.dependents("mid"), (std::vector<std::string>{"top", "aaa"}));
    CHECK_EQ(g.dependents("aaa"), std::vector<std::string>{});
    DepGraph same_level;
    for (const char* n : {"core", "x", "b", "c"}) same_level.add_node(n);
    same_level.add_edge("x", "core");
    same_level.add_edge("b", "core");
    same_level.add_edge("c", "core");
    CHECK_EQ(same_level.dependents("core"), (std::vector<std::string>{"b", "c", "x"}));
    DepGraph diamond;
    for (const char* n : {"a", "b", "c", "d"}) diamond.add_node(n);
    diamond.add_edge("a", "b");
    diamond.add_edge("a", "c");
    diamond.add_edge("b", "d");
    diamond.add_edge("c", "d");
    CHECK_EQ(diamond.dependents("d"), (std::vector<std::string>{"b", "c", "a"}));
    CHECK_EQ(diamond.dependents("a"), std::vector<std::string>{});
    DepGraph loop;
    for (const char* n : {"p", "q", "r"}) loop.add_node(n);
    loop.add_edge("p", "q");
    loop.add_edge("q", "r");
    loop.add_edge("r", "p");
    CHECK_EQ(loop.dependents("p"), (std::vector<std::string>{"r", "q"}));
    DepGraph self;
    self.add_node("s");
    self.add_edge("s", "s");
    CHECK_EQ(self.dependents("s"), std::vector<std::string>{});
    DepGraph missing;
    missing.add_node("m");
    missing.add_edge("m", "gone");
    CHECK_EQ(missing.dependents("gone"), (std::vector<std::string>{"m"}));
    CHECK_EQ(missing.dependents("nobody"), std::vector<std::string>{});
}
// checks if a repeated word only posts the document once
TEST(index_postings_once_per_document) {
    SkillIndex ix;
    ix.add(0, "table table table");
    ix.add(1, "table report");
    REQUIRE(ix.postings("table") != nullptr);
    CHECK_EQ(*ix.postings("table"), (std::vector<std::size_t>{0, 1}));
    REQUIRE(ix.postings("report") != nullptr);
    CHECK_EQ(*ix.postings("report"), (std::vector<std::size_t>{1}));
    CHECK_EQ(ix.word_count(), std::size_t{2});
    CHECK(ix.postings("nothing") == nullptr);
    CHECK_EQ(ix.search("", 5).size(), std::size_t{0});
    CHECK_EQ(ix.search("table", 0).size(), std::size_t{0});
    CHECK_EQ(ix.search("zzz", 5).size(), std::size_t{0});
    ix.add(2, "Table REPORT!");
    CHECK_EQ(*ix.postings("table"), (std::vector<std::size_t>{0, 1, 2}));
    CHECK_EQ(ix.search("report report report", 10), (std::vector<std::size_t>{1, 2}));
    CHECK_EQ(ix.search("table report", 10), (std::vector<std::size_t>{1, 2, 0}));
}

// prints how long search takes vs linear_search
TEST(index_timing_vs_linear_search) {
    int sizes[] = {100, 1000, 5000};
    for (int n : sizes) {
        std::mt19937 rng(n);
        std::vector<std::string> docs;
        SkillIndex ix;
        for (int i = 0; i < n; i++) {
            std::string text;
            for (int w = 0; w < 8; w++) {
                text += "w" + std::to_string(rng() % 200) + " ";
            }
            docs.push_back(text);
            ix.add(i, text);
        }
        CHECK_EQ(ix.search("w1 w2", 5), linear_search(docs, "w1 w2", 5));
        const int queries = 20;
        auto t0 = std::chrono::steady_clock::now();
        for (int q = 0; q < queries; q++) {
            ix.search("w1 w2", 5);
        }
        auto t1 = std::chrono::steady_clock::now();
        for (int q = 0; q < queries; q++) {
            linear_search(docs, "w1 w2", 5);
        }
        auto t2 = std::chrono::steady_clock::now();
        double index_ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / queries;
        double linear_ms = std::chrono::duration<double, std::milli>(t2 - t1).count() / queries;
        std::printf("    %5d docs: index %.4f ms/query, linear %.4f ms/query\n", n, index_ms, linear_ms);
    }
}

int main(int argc, char** argv) { return mt::run_all(argc, argv); }


