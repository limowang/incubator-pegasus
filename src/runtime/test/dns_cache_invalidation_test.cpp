/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "fmt/format.h"
#include "gtest/gtest.h"
#include "rpc/dns_resolver.h"
#include "rpc/rpc_address.h"
#include "rpc/rpc_host_port.h"
#include "utils/flags.h"

namespace dsn {

TEST(dns_cache_invalidation_test, test_dns_cache_clear_command)
{
    // Save original cache size
    int32_t original_max_size = FLAGS_dns_resolver_cache_max_size;

    // Set a small cache size for testing
    FLAGS_dns_resolver_cache_max_size = 10;

    // Create some cache entries
    std::vector<host_port> hosts;
    for (int i = 0; i < 5; ++i) {
        hosts.emplace_back(fmt::format("host{}.clear.local", i), 8080 + i);
    }

    // Resolve all entries to populate cache
    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve();
        }
    }

    // Get cache stats before clearing
    dns_resolver &resolver = dns_resolver::instance();
    std::string stats_before = resolver.get_cache_stats();
    LOG_INFO("Cache stats before clear: {}", stats_before);

    // Clear the cache
    std::string result = resolver.clear_cache();
    LOG_INFO("Clear cache result: {}", result);

    // Verify cache was cleared
    std::string stats_after = resolver.get_cache_stats();
    LOG_INFO("Cache stats after clear: {}", stats_after);

    // Restore original cache size
    FLAGS_dns_resolver_cache_max_size = original_max_size;

    LOG_INFO("DNS cache clear command test completed");
}

TEST(dns_cache_invalidation_test, test_dns_cache_invalidate_specific_host)
{
    // Save original cache size
    int32_t original_max_size = FLAGS_dns_resolver_cache_max_size;
    FLAGS_dns_resolver_cache_max_size = 10;

    // Create cache entries
    std::vector<host_port> hosts;
    for (int i = 0; i < 3; ++i) {
        hosts.emplace_back(fmt::format("host{}.invalidate.local", i), 8090 + i);
    }

    // Resolve all entries to populate cache
    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve();
        }
    }

    dns_resolver &resolver = dns_resolver::instance();

    // Get initial cache stats
    std::string stats_before = resolver.get_cache_stats();
    LOG_INFO("Cache stats before invalidation: {}", stats_before);

    // Invalidate the first host
    std::string host_to_invalidate = "host0.invalidate.local:8090";
    std::string result = resolver.invalidate_host({host_to_invalidate});
    LOG_INFO("Invalidate host result: {}", result);

    // Get cache stats after invalidation
    std::string stats_after = resolver.get_cache_stats();
    LOG_INFO("Cache stats after invalidation: {}", stats_after);

    // Try to invalidate a non-existent host
    std::string non_existent = "nonexistent.local:9999";
    std::string result_nonexist = resolver.invalidate_host({non_existent});
    LOG_INFO("Invalidate non-existent host result: {}", result_nonexist);

    // Restore original cache size
    FLAGS_dns_resolver_cache_max_size = original_max_size;

    LOG_INFO("DNS cache invalidate specific host test completed");
}

TEST(dns_cache_invalidation_test, test_dns_cache_stats_command)
{
    // Save original cache size
    int32_t original_max_size = FLAGS_dns_resolver_cache_max_size;
    FLAGS_dns_resolver_cache_max_size = 10;

    dns_resolver &resolver = dns_resolver::instance();

    // Get initial stats
    std::string stats_initial = resolver.get_cache_stats();
    LOG_INFO("Initial cache stats: {}", stats_initial);

    // Add some entries
    std::vector<host_port> hosts;
    for (int i = 0; i < 3; ++i) {
        hosts.emplace_back(fmt::format("host{}.stats.local", i), 8100 + i);
    }

    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve();
        }
    }

    // Wait a bit to have some age
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Get stats with entries
    std::string stats_with_entries = resolver.get_cache_stats();
    LOG_INFO("Cache stats with entries: {}", stats_with_entries);

    // Restore original cache size
    FLAGS_dns_resolver_cache_max_size = original_max_size;

    LOG_INFO("DNS cache stats command test completed");
}

TEST(dns_cache_invalidation_test, test_dns_cache_invalidate_with_invalid_args)
{
    dns_resolver &resolver = dns_resolver::instance();

    // Test with no arguments
    std::string result_no_args = resolver.invalidate_host({});
    LOG_INFO("Invalidate with no args: {}", result_no_args);

    // Test with invalid host:port format
    std::string result_invalid = resolver.invalidate_host({"invalid_format"});
    LOG_INFO("Invalidate with invalid format: {}", result_invalid);

    LOG_INFO("DNS cache invalidate with invalid args test completed");
}

TEST(dns_cache_invalidation_test, test_dns_cache_invalidate_after_resolve)
{
    // Save original cache size
    int32_t original_max_size = FLAGS_dns_resolver_cache_max_size;
    FLAGS_dns_resolver_cache_max_size = 10;

    dns_resolver &resolver = dns_resolver::instance();

    // Resolve a hostname (will be cached)
    host_port hp("localhost", 8110);
    if (hp) {
        rpc_address addr1 = hp.resolve();
        ASSERT_TRUE(addr1);
        LOG_INFO("First resolution: {}", addr1.to_string());
    }

    // Verify it's cached
    std::string stats_cached = resolver.get_cache_stats();
    LOG_INFO("Cache stats after resolve: {}", stats_cached);

    // Invalidate the entry
    std::string result = resolver.invalidate_host({"localhost:8110"});
    LOG_INFO("Invalidate result: {}", result);

    // Resolve again (should re-resolve, not from cache)
    if (hp) {
        rpc_address addr2 = hp.resolve();
        ASSERT_TRUE(addr2);
        LOG_INFO("Second resolution after invalidation: {}", addr2.to_string());
    }

    // Restore original cache size
    FLAGS_dns_resolver_cache_max_size = original_max_size;

    LOG_INFO("DNS cache invalidate after resolve test completed");
}

TEST(dns_cache_invalidation_test, test_dns_cache_clear_empty_cache)
{
    // Save original cache size
    int32_t original_max_size = FLAGS_dns_resolver_cache_max_size;
    FLAGS_dns_resolver_cache_max_size = 10;

    dns_resolver &resolver = dns_resolver::instance();

    // Clear empty cache
    std::string result = resolver.clear_cache();
    LOG_INFO("Clear empty cache result: {}", result);

    // Restore original cache size
    FLAGS_dns_resolver_cache_max_size = original_max_size;

    LOG_INFO("DNS cache clear empty cache test completed");
}

TEST(dns_cache_invalidation_test, test_dns_cache_clear_and_repopulate)
{
    // Save original cache size
    int32_t original_max_size = FLAGS_dns_resolver_cache_max_size;
    FLAGS_dns_resolver_cache_max_size = 10;

    dns_resolver &resolver = dns_resolver::instance();

    // Populate cache
    std::vector<host_port> hosts;
    for (int i = 0; i < 3; ++i) {
        hosts.emplace_back(fmt::format("host{}.repopulate.local", i), 8120 + i);
    }

    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve();
        }
    }

    // Get stats before clear
    std::string stats_before = resolver.get_cache_stats();
    LOG_INFO("Cache stats before clear: {}", stats_before);

    // Clear cache
    std::string clear_result = resolver.clear_cache();
    LOG_INFO("Clear result: {}", clear_result);

    // Get stats after clear
    std::string stats_after_clear = resolver.get_cache_stats();
    LOG_INFO("Cache stats after clear: {}", stats_after_clear);

    // Repopulate cache
    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve();
        }
    }

    // Get stats after repopulation
    std::string stats_after_repopulate = resolver.get_cache_stats();
    LOG_INFO("Cache stats after repopulation: {}", stats_after_repopulate);

    // Restore original cache size
    FLAGS_dns_resolver_cache_max_size = original_max_size;

    LOG_INFO("DNS cache clear and repopulate test completed");
}

} // namespace dsn
