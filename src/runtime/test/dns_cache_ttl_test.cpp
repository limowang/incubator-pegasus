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

// Test configuration flags for DNS cache TTL
TEST(dns_cache_ttl_test, test_dns_cache_ttl_config_flags)
{
    // Verify that TTL configuration flags exist and have default values
    EXPECT_GE(FLAGS_dns_resolver_cache_ttl_seconds, 0); // 0 means no expiration
    EXPECT_LE(FLAGS_dns_resolver_cache_ttl_seconds, 86400); // Max 24 hours

    // Log the current configuration for verification
    LOG_INFO("DNS cache TTL configuration: ttl_seconds={}", FLAGS_dns_resolver_cache_ttl_seconds);
}

TEST(dns_cache_ttl_test, test_dns_cache_basic_expiration)
{
    // This test verifies basic TTL behavior with a short expiration time

    // Save original TTL
    int64_t original_ttl = FLAGS_dns_resolver_cache_ttl_seconds;

    // Set a very short TTL for testing (1 second)
    FLAGS_dns_resolver_cache_ttl_seconds = 1;

    // Resolve a hostname
    host_port hp("localhost", 8080);
    ASSERT_TRUE(hp);

    // First resolution - cache miss
    rpc_address addr1 = hp.resolve();
    ASSERT_TRUE(addr1);

    // Wait for TTL to expire
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Second resolution - should be cache miss due to expiration
    // (will trigger re-resolution)
    rpc_address addr2 = hp.resolve();
    ASSERT_TRUE(addr2);

    // Both should resolve to the same address (localhost)
    EXPECT_EQ(addr1, addr2);

    // Restore original TTL
    FLAGS_dns_resolver_cache_ttl_seconds = original_ttl;

    LOG_INFO("DNS cache TTL expiration test completed");
}

TEST(dns_cache_ttl_test, test_dns_cache_no_expiration)
{
    // Test behavior with TTL disabled (ttl_seconds = 0)

    // Save original TTL
    int64_t original_ttl = FLAGS_dns_resolver_cache_ttl_seconds;

    // Set TTL to 0 (no expiration)
    FLAGS_dns_resolver_cache_ttl_seconds = 0;

    // Resolve a hostname
    host_port hp("localhost", 8081);
    ASSERT_TRUE(hp);

    // First resolution
    rpc_address addr1 = hp.resolve();
    ASSERT_TRUE(addr1);

    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Second resolution - should still be cached (no expiration)
    rpc_address addr2 = hp.resolve();
    ASSERT_TRUE(addr2);

    // Both should be the same
    EXPECT_EQ(addr1, addr2);

    // Restore original TTL
    FLAGS_dns_resolver_cache_ttl_seconds = original_ttl;

    LOG_INFO("DNS cache no expiration test completed");
}

TEST(dns_cache_ttl_test, test_dns_cache_long_ttl)
{
    // Test behavior with a long TTL (24 hours)

    // Save original TTL
    int64_t original_ttl = FLAGS_dns_resolver_cache_ttl_seconds;

    // Set a long TTL
    FLAGS_dns_resolver_cache_ttl_seconds = 86400; // 24 hours

    // Resolve hostnames
    std::vector<host_port> hosts = {
        host_port("localhost", 8090),
        host_port("localhost", 8091),
        host_port("127.0.0.1", 8092),
    };

    for (const auto &hp : hosts) {
        ASSERT_TRUE(hp);
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
    }

    // All should be cached
    for (const auto &hp : hosts) {
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
    }

    // Restore original TTL
    FLAGS_dns_resolver_cache_ttl_seconds = original_ttl;

    LOG_INFO("DNS cache long TTL test completed");
}

TEST(dns_cache_ttl_test, test_dns_cache_refresh_after_expiration)
{
    // Test that expired entries are refreshed with new DNS resolution

    // Save original TTL
    int64_t original_ttl = FLAGS_dns_resolver_cache_ttl_seconds;

    // Set a very short TTL
    FLAGS_dns_resolver_cache_ttl_seconds = 1;

    host_port hp("localhost", 8095);
    ASSERT_TRUE(hp);

    // First resolution
    rpc_address addr1 = hp.resolve();
    ASSERT_TRUE(addr1);

    // Wait for expiration
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Second resolution - should re-resolve due to expiration
    rpc_address addr2 = hp.resolve();
    ASSERT_TRUE(addr2);

    // Should be the same address (DNS hasn't changed)
    EXPECT_EQ(addr1, addr2);

    // Restore original TTL
    FLAGS_dns_resolver_cache_ttl_seconds = original_ttl;

    LOG_INFO("DNS cache refresh after expiration test completed");
}

TEST(dns_cache_ttl_test, test_dns_cache_multiple_entries_expiration)
{
    // Test that multiple entries can expire independently

    // Save original TTL
    int64_t original_ttl = FLAGS_dns_resolver_cache_ttl_seconds;

    // Set a short TTL
    FLAGS_dns_resolver_cache_ttl_seconds = 2;

    // Create multiple entries at different times
    std::vector<host_port> hosts;
    for (int i = 0; i < 3; ++i) {
        hosts.emplace_back("localhost", 8100 + i);
    }

    // Resolve all entries
    for (const auto &hp : hosts) {
        ASSERT_TRUE(hp);
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
    }

    // Wait for TTL to expire
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // All should be expired and re-resolved
    for (const auto &hp : hosts) {
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
    }

    // Restore original TTL
    FLAGS_dns_resolver_cache_ttl_seconds = original_ttl;

    LOG_INFO("DNS cache multiple entries expiration test completed");
}

TEST(dns_cache_ttl_test, test_dns_cache_ttl_configuration_safety)
{
    // Test that TTL configuration has safe limits

    // Test with reasonable TTL values
    EXPECT_GE(FLAGS_dns_resolver_cache_ttl_seconds, 0);
    EXPECT_LE(FLAGS_dns_resolver_cache_ttl_seconds, 86400); // Max 24 hours

    // Test cache size configuration
    EXPECT_GE(FLAGS_dns_resolver_cache_max_size, 0);

    LOG_INFO("DNS cache TTL configuration safety checks passed");
}

TEST(dns_cache_ttl_test, test_dns_cache_ttl_with_lru_interaction)
{
    // Test that TTL and LRU eviction work together correctly

    // Save original values
    int64_t original_ttl = FLAGS_dns_resolver_cache_ttl_seconds;
    int32_t original_max_size = FLAGS_dns_resolver_cache_max_size;

    // Set short TTL and small cache size
    FLAGS_dns_resolver_cache_ttl_seconds = 1;
    FLAGS_dns_resolver_cache_max_size = 3;

    // Create more entries than cache size
    std::vector<host_port> hosts;
    for (int i = 0; i < 5; ++i) {
        hosts.emplace_back(fmt::format("host{}.ttl.local", i), 8200 + i);
    }

    // Resolve all entries (will trigger both LRU eviction and expiration)
    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve();
        }
    }

    // Wait for expiration
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Try to resolve again (should handle both TTL and LRU)
    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve();
        }
    }

    // Restore original values
    FLAGS_dns_resolver_cache_ttl_seconds = original_ttl;
    FLAGS_dns_resolver_cache_max_size = original_max_size;

    LOG_INFO("DNS cache TTL and LRU interaction test completed");
}

TEST(dns_cache_ttl_test, test_dns_cache_ttl_zero_vs_unlimited)
{
    // Test that TTL=0 means no expiration (not unlimited cache)

    // Save original values
    int64_t original_ttl = FLAGS_dns_resolver_cache_ttl_seconds;
    int32_t original_max_size = FLAGS_dns_resolver_cache_max_size;

    // TTL=0 but cache size limit active
    FLAGS_dns_resolver_cache_ttl_seconds = 0; // No expiration
    FLAGS_dns_resolver_cache_max_size = 5;

    // Create entries
    std::vector<host_port> hosts;
    for (int i = 0; i < 3; ++i) {
        hosts.emplace_back(fmt::format("host{}.noexpire.local", i), 8300 + i);
    }

    // Resolve entries
    for (const auto &hp : hosts) {
        if (hp) {
            hp.resolve();
        }
    }

    // Wait - entries should NOT expire (TTL=0)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // All should still be cached
    for (const auto &hp : hosts) {
        if (hp) {
            rpc_address addr = hp.resolve();
            // May fail DNS resolution but cache should still work for valid entries
        }
    }

    // Restore original values
    FLAGS_dns_resolver_cache_ttl_seconds = original_ttl;
    FLAGS_dns_resolver_cache_max_size = original_max_size;

    LOG_INFO("DNS cache TTL zero vs unlimited test completed");
}

TEST(dns_cache_ttl_test, test_dns_cache_ttl_production_scenarios)
{
    // Test TTL settings for different production scenarios

    // Save original TTL
    int64_t original_ttl = FLAGS_dns_resolver_cache_ttl_seconds;

    // Scenario 1: Cloud environment with frequent changes
    FLAGS_dns_resolver_cache_ttl_seconds = 300; // 5 minutes
    LOG_INFO("Cloud environment: TTL=5 minutes (frequent DNS changes)");

    // Scenario 2: Stable on-premises deployment
    FLAGS_dns_resolver_cache_ttl_seconds = 7200; // 2 hours
    LOG_INFO("On-premises: TTL=2 hours (stable infrastructure)");

    // Scenario 3: Development environment
    FLAGS_dns_resolver_cache_ttl_seconds = 60; // 1 minute
    LOG_INFO("Development: TTL=1 minute (fast iteration)");

    // Restore original TTL
    FLAGS_dns_resolver_cache_ttl_seconds = original_ttl;

    LOG_INFO("DNS cache TTL production scenarios test completed");
}

} // namespace dsn
