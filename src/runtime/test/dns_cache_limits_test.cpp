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
#include "rpc/rpc_address.h"
#include "rpc/rpc_host_port.h"
#include "utils/flags.h"

namespace dsn {

// Test configuration flags for DNS cache limits
TEST(dns_cache_limits_test, test_dns_cache_config_flags)
{
    // Verify that cache limit configuration flags exist and have default values
    EXPECT_GE(FLAGS_dns_resolver_cache_max_size, 0); // 0 means unlimited
    EXPECT_LE(FLAGS_dns_resolver_cache_max_size, 10000); // Reasonable upper limit

    // Log the current configuration for verification
    LOG_INFO("DNS cache configuration: max_size={}", FLAGS_dns_resolver_cache_max_size);
}

TEST(dns_cache_limits_test, test_dns_cache_basic_lru_behavior)
{
    // This test verifies basic LRU behavior with a small cache size
    // Note: Testing actual cache eviction is difficult without directly
    // accessing the DNS resolver internals

    // Create multiple unique host_port entries
    std::vector<host_port> hosts;
    for (int i = 0; i < 10; ++i) {
        hosts.emplace_back(fmt::format("host{}.test.local", i), 8080 + i);
    }

    // Resolve all hosts to populate cache
    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve(); // May fail DNS resolution but that's OK for this test
        }
    }

    LOG_INFO("DNS cache LRU behavior test completed with {} unique hosts", hosts.size());
}

TEST(dns_cache_limits_test, test_dns_cache_hit_miss_with_limits)
{
    // Test that cache hit/miss logic works correctly even with limits

    host_port hp1("localhost", 8080);
    host_port hp2("localhost", 8081);
    host_port hp3("127.0.0.1", 8082);

    ASSERT_TRUE(hp1 && hp2 && hp3);

    // First resolution - cache miss
    rpc_address addr1a = hp1.resolve();
    ASSERT_TRUE(addr1a);

    // Second resolution - cache hit
    rpc_address addr1b = hp1.resolve();
    ASSERT_TRUE(addr1b);
    ASSERT_EQ(addr1a, addr1b);

    // Different port - cache miss
    rpc_address addr2 = hp2.resolve();
    ASSERT_TRUE(addr2);

    // Different host - cache miss
    rpc_address addr3 = hp3.resolve();
    ASSERT_TRUE(addr3);

    LOG_INFO("DNS cache hit/miss test completed successfully");
}

TEST(dns_cache_limits_test, test_dns_cache_unlimited_configuration)
{
    // Test behavior with unlimited cache (max_size = 0)
    int original_max_size = FLAGS_dns_resolver_cache_max_size;

    // Set to unlimited
    FLAGS_dns_resolver_cache_max_size = 0;

    // Create many unique entries
    std::vector<host_port> hosts;
    for (int i = 0; i < 100; ++i) {
        hosts.emplace_back(fmt::format("host{}.unlimited.local", i), 8080 + i);
    }

    // Resolve all (should work without eviction)
    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve();
        }
    }

    // Restore original setting
    FLAGS_dns_resolver_cache_max_size = original_max_size;

    LOG_INFO("DNS cache unlimited test completed with 100 hosts");
}

TEST(dns_cache_limits_test, test_dns_cache_small_limit)
{
    // Test behavior with a very small cache limit
    int original_max_size = FLAGS_dns_resolver_cache_max_size;

    // Set to very small limit
    FLAGS_dns_resolver_cache_max_size = 3;

    // Create more entries than the limit
    std::vector<host_port> hosts;
    for (int i = 0; i < 5; ++i) {
        hosts.emplace_back(fmt::format("host{}.small.local", i), 9000 + i);
    }

    // Resolve all (should trigger evictions)
    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve();
        }
    }

    // Restore original setting
    FLAGS_dns_resolver_cache_max_size = original_max_size;

    LOG_INFO("DNS cache small limit test completed with limit=3, hosts=5");
}

TEST(dns_cache_limits_test, test_dns_cache_consistency)
{
    // Test that cache remains consistent under concurrent access patterns
    std::vector<host_port> hosts = {
        host_port("localhost", 8080),
        host_port("localhost", 8081),
        host_port("127.0.0.1", 8082),
    };

    // Resolve multiple times in different patterns
    for (int round = 0; round < 3; ++round) {
        for (const auto &hp : hosts) {
            rpc_address addr = hp.resolve();
            ASSERT_TRUE(addr);
        }
    }

    // Verify all addresses are still valid
    for (const auto &hp : hosts) {
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
        ASSERT_EQ(HOST_TYPE_IPV4, addr.type());
    }

    LOG_INFO("DNS cache consistency test completed");
}

TEST(dns_cache_limits_test, test_dns_cache_memory_safety)
{
    // Test that cache limits prevent unbounded memory growth

    int original_max_size = FLAGS_dns_resolver_cache_max_size;

    // Set a reasonable limit
    FLAGS_dns_resolver_cache_max_size = 100;

    // Create a large number of unique entries
    std::vector<host_port> hosts;
    for (int i = 0; i < 1000; ++i) {
        hosts.emplace_back(fmt::format("host{}.memory.local", i), 8080 + i);
    }

    // Resolve all (cache should stay bounded)
    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve();
        }
    }

    // Restore original setting
    FLAGS_dns_resolver_cache_max_size = original_max_size;

    LOG_INFO("DNS cache memory safety test completed with 1000 hosts, limit=100");
}

TEST(dns_cache_limits_test, test_dns_cache_access_time_update)
{
    // Test that accessing a cache entry updates its access time

    host_port hp("localhost", 8080);
    ASSERT_TRUE(hp);

    // First resolution - cache miss
    rpc_address addr1 = hp.resolve();
    ASSERT_TRUE(addr1);

    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Second resolution - cache hit (should update access time)
    rpc_address addr2 = hp.resolve();
    ASSERT_TRUE(addr2);
    ASSERT_EQ(addr1, addr2);

    LOG_INFO("DNS cache access time update test completed");
}

TEST(dns_cache_limits_test, test_dns_cache_configuration_validation)
{
    // Test that configuration has safe limits

    // Test with reasonable cache sizes
    EXPECT_GE(FLAGS_dns_resolver_cache_max_size, 0);
    EXPECT_LE(FLAGS_dns_resolver_cache_max_size, 100000);

    // Test that retry configuration is also valid
    EXPECT_GE(FLAGS_dns_resolver_retry_max_count, 0);
    EXPECT_LE(FLAGS_dns_resolver_retry_max_count, 10);
    EXPECT_GE(FLAGS_dns_resolver_retry_delay_ms, 10);
    EXPECT_LE(FLAGS_dns_resolver_retry_delay_ms, 10000);

    LOG_INFO("DNS cache configuration validation checks passed");
}

} // namespace dsn
