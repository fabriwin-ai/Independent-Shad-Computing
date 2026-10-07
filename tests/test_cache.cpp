#include <string>
#include <vector>

#include "isc/core/resource_cache.hpp"
#include "isc_test.hpp"

using namespace isc;

ISC_TEST(cache_hit_and_miss) {
    ResourceCache<int> c(100);
    CHECK(!c.take({1, 1}).has_value());
    c.put({1, 1}, 42, 10);
    auto v = c.take({1, 1});
    CHECK(v.has_value());
    CHECK_EQ(*v, 42);
    CHECK_EQ(c.stats().hits, 1u);
    CHECK_EQ(c.stats().misses, 1u);
    CHECK_EQ(c.stats().bytes_cached, 0u);
}

ISC_TEST(cache_lru_eviction_under_budget) {
    std::vector<int> evicted;
    ResourceCache<int> c(30, [&](int& v) { evicted.push_back(v); });
    c.put({1, 0}, 1, 10);
    c.put({2, 0}, 2, 10);
    c.put({3, 0}, 3, 10);
    CHECK(evicted.empty());
    c.put({4, 0}, 4, 10);  // over budget -> oldest (1) goes
    CHECK_EQ(evicted.size(), 1u);
    CHECK_EQ(evicted[0], 1);
    CHECK_EQ(c.stats().bytes_cached, 30u);
    c.set_budget(10);  // shrink -> 2 and 3 go
    CHECK_EQ(evicted.size(), 3u);
    CHECK_EQ(c.stats().entries, 1u);
    CHECK(c.take({4, 0}).has_value());
}

ISC_TEST(cache_oversized_released_immediately) {
    int evictions = 0;
    ResourceCache<int> c(10, [&](int&) { ++evictions; });
    c.put({1, 0}, 1, 11);
    CHECK_EQ(evictions, 1);
    CHECK_EQ(c.stats().entries, 0u);
}

ISC_TEST(cache_clear_and_destructor_release_everything) {
    int evictions = 0;
    {
        ResourceCache<int> c(100, [&](int&) { ++evictions; });
        c.put({1, 0}, 1, 10);
        c.put({2, 0}, 2, 10);
        c.clear();
        CHECK_EQ(evictions, 2);
        c.put({3, 0}, 3, 10);
    }
    CHECK_EQ(evictions, 3);
}

ISC_TEST(cache_prefers_most_recent_match) {
    ResourceCache<std::string> c(100);
    c.put({7, 7}, "old", 1);
    c.put({7, 7}, "new", 1);
    CHECK_EQ(*c.take({7, 7}), std::string("new"));
    CHECK_EQ(*c.take({7, 7}), std::string("old"));
}
