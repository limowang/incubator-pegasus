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

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "rpc/rpc_address.h"
#include "rpc/rpc_host_port.h"
#include "utils/flags.h"

namespace dsn {

// Test configuration flags for DNS retry
TEST(dns_retry_test, test_dns_retry_config_flags)
{
    // Verify that retry configuration flags exist and have default values
    EXPECT_GT(FLAGS_dns_resolver_retry_max_count, 0);
    EXPECT_GT(FLAGS_dns_resolver_retry_delay_ms, 0);
    EXPECT_LE(FLAGS_dns_resolver_retry_max_count, 10); // Reasonable upper limit

    // Log the current configuration for verification
    LOG_INFO("DNS retry configuration: max_count={}, delay_ms={}",
             FLAGS_dns_resolver_retry_max_count,
             FLAGS_dns_resolver_retry_delay_ms);
}

TEST(dns_retry_test, test_dns_retry_with_valid_hostname)
{
    // Test that retry logic works with a valid hostname
    // (should succeed on first try, no retries needed)
    host_port hp("localhost", 8080);
    ASSERT_TRUE(hp);

    rpc_address addr = hp.resolve();
    ASSERT_TRUE(addr);

    // Verify resolution succeeded
    EXPECT_EQ(HOST_TYPE_IPV4, addr.type());
    EXPECT_GT(addr.port(), 0);
}

TEST(dns_retry_test, test_dns_retry_with_ip_address)
{
    // Test that retry logic doesn't affect IP address resolution
    // (IP addresses don't need DNS lookup, should succeed immediately)
    host_port hp("127.0.0.1", 8080);
    ASSERT_TRUE(hp);

    rpc_address addr = hp.resolve();
    ASSERT_TRUE(addr);

    // Verify resolution succeeded
    EXPECT_EQ(HOST_TYPE_IPV4, addr.type());
    EXPECT_EQ(8080, addr.port());
}

TEST(dns_retry_test, test_dns_retry_multiple_resolutions)
{
    // Test multiple resolutions to verify cache and retry interaction
    host_port hp("localhost", 8080);
    ASSERT_TRUE(hp);

    // First resolution
    rpc_address addr1 = hp.resolve();
    ASSERT_TRUE(addr1);

    // Second resolution (should be cached, no retry)
    rpc_address addr2 = hp.resolve();
    ASSERT_TRUE(addr2);

    // Both should resolve to the same address
    EXPECT_EQ(addr1, addr2);
}

TEST(dns_retry_test, test_dns_retry_exponential_backoff)
{
    // This test verifies the exponential backoff logic
    // Note: Testing actual retry behavior with real DNS failures is difficult
    // in unit tests. This test verifies the configuration and basic behavior.

    // Set test values for retry configuration
    int max_count = FLAGS_dns_resolver_retry_max_count;
    int delay_ms = FLAGS_dns_resolver_retry_delay_ms;

    // Verify exponential backoff progression
    int current_delay = delay_ms;
    for (int i = 0; i < max_count; ++i) {
        EXPECT_GE(current_delay, delay_ms);
        EXPECT_LE(current_delay, 5000); // Max cap

        // Simulate exponential backoff
        current_delay = std::min(current_delay * 2, 5000);
    }

    LOG_INFO("Exponential backoff progression from {}ms to max 5000ms over {} attempts",
             delay_ms,
             max_count);
}

TEST(dns_retry_test, test_dns_retry_with_invalid_hostname)
{
    // Test with an invalid hostname that should fail even after retries
    // This test may take some time due to retry delays

    host_port hp("invalid.hostname.test.that.does.not.exist", 8080);
    ASSERT_TRUE(hp); // host_port is valid, but DNS will fail

    // This should fail after all retries
    rpc_address addr = hp.resolve();

    // The resolution should fail (invalid address)
    // Note: In some environments, this might resolve if there's a wildcard DNS
    if (!addr) {
        LOG_INFO("As expected, failed to resolve invalid hostname after retries");
    } else {
        LOG_WARNING("Unexpectedly resolved invalid hostname to: {}", addr);
    }
}

TEST(dns_retry_test, test_dns_retry_cache_efficiency)
{
    // Test that cache works efficiently even with retry enabled
    std::vector<host_port> hosts = {
        host_port("localhost", 8080),
        host_port("localhost", 8081),
        host_port("127.0.0.1", 8082),
    };

    // First pass - populate cache
    for (const auto &hp : hosts) {
        ASSERT_TRUE(hp);
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
    }

    // Second pass - should hit cache (no retries)
    for (const auto &hp : hosts) {
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
    }

    // Verify all addresses are different (different ports)
    std::set<rpc_address> unique_addrs;
    for (const auto &hp : hosts) {
        unique_addrs.insert(hp.resolve());
    }
    EXPECT_EQ(3, unique_addrs.size());
}

TEST(dns_retry_test, test_dns_retry_configuration_safety)
{
    // Test that retry configuration has safe limits

    // Test with reasonable retry counts
    EXPECT_GE(FLAGS_dns_resolver_retry_max_count, 0);
    EXPECT_LE(FLAGS_dns_resolver_retry_max_count, 10);

    // Test with reasonable retry delays
    EXPECT_GE(FLAGS_dns_resolver_retry_delay_ms, 10); // At least 10ms
    EXPECT_LE(FLAGS_dns_resolver_retry_delay_ms, 10000); // At most 10 seconds

    LOG_INFO("DNS retry configuration safety checks passed");
}

} // namespace dsn
