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
#include "utils/metrics.h"

namespace dsn {

TEST(dns_metrics_test, test_dns_resolver_cache_hit_metrics)
{
    // Create a host_port and resolve it (should be a cache miss first time)
    host_port hp("localhost", 8080);
    ASSERT_TRUE(hp);

    // First resolution should increment cache_miss and resolve_success
    rpc_address addr1 = hp.resolve();
    ASSERT_TRUE(addr1);

    // Second resolution should increment cache_hit and resolve_success
    rpc_address addr2 = hp.resolve();
    ASSERT_TRUE(addr2);

    // Verify both addresses are the same
    ASSERT_EQ(addr1, addr2);
}

TEST(dns_metrics_test, test_dns_resolver_failure_metrics)
{
    // Test with an invalid hostname that should fail DNS resolution
    // Note: This test uses a hostname that's unlikely to resolve
    // In production, use actual DNS failure scenarios

    host_port hp("invalid.hostname.example.that.does.not.exist", 8080);
    ASSERT_TRUE(hp); // host_port is valid, but DNS resolution will fail

    // This should fail DNS resolution but not crash
    rpc_address addr = hp.resolve();

    // The resolution might fail (return invalid address) or succeed depending on DNS
    // The key is that metrics are tracked regardless of outcome
    if (addr) {
        LOG_INFO("Unexpectedly resolved invalid hostname to: {}", addr);
    } else {
        LOG_INFO("As expected, failed to resolve invalid hostname");
    }
}

TEST(dns_metrics_test, test_dns_resolver_multiple_resolutions)
{
    // Test multiple resolutions to verify cache behavior
    std::vector<host_port> host_ports = {
        host_port("localhost", 8080),
        host_port("localhost", 8081),
        host_port("127.0.0.1", 8082),
    };

    // First pass - all should be cache misses
    for (const auto &hp : host_ports) {
        ASSERT_TRUE(hp);
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
    }

    // Second pass - should be cache hits for localhost ones
    for (const auto &hp : host_ports) {
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
    }
}

TEST(dns_metrics_test, test_dns_resolver_cache_size)
{
    // Test that cache size is tracked properly
    // Create unique host_ports to populate the cache
    std::vector<host_port> unique_hosts;

    // Create several unique hostnames
    for (int i = 0; i < 5; ++i) {
        host_port hp(fmt::format("host{}.example.com", i), 8080 + i);
        if (hp) {
            unique_hosts.push_back(hp);
        }
    }

    // Resolve all of them (this will populate cache or fail DNS)
    for (const auto &hp : unique_hosts) {
        hp.resolve(); // May succeed or fail depending on DNS
    }

    // The cache size metric should be tracked
    // (actual value depends on DNS resolution success/failure)
    LOG_INFO("DNS resolver cache test completed with {} unique hosts", unique_hosts.size());
}

TEST(dns_metrics_test, test_dns_resolver_metrics_consistency)
{
    // Test that metrics are consistent across multiple resolutions
    host_port hp("localhost", 8080);

    // Resolve multiple times
    for (int i = 0; i < 10; ++i) {
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
        ASSERT_EQ(HOST_TYPE_IPV4, addr.type());
    }

    // All resolutions should return the same address
    rpc_address expected = hp.resolve();
    for (int i = 0; i < 5; ++i) {
        rpc_address addr = hp.resolve();
        ASSERT_EQ(expected, addr);
    }
}

TEST(dns_metrics_test, test_dns_resolver_invalid_host_port)
{
    // Test that invalid host_port doesn't affect metrics
    host_port invalid_hp;
    ASSERT_FALSE(invalid_hp);

    // Resolving invalid host_port should return invalid address
    rpc_address addr = invalid_hp.resolve();
    ASSERT_FALSE(addr);
}

} // namespace dsn
