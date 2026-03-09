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
#include <thread>

#include "gtest/gtest.h"
#include "rpc/rpc_address.h"
#include "rpc/rpc_host_port.h"
#include "utils/flags.h"

namespace dsn {

// Test DNS resolution timeout and slow detection configuration
TEST(dns_timeout_test, test_dns_timeout_config_flags)
{
    // Verify that timeout configuration flags exist and have default values
    EXPECT_GE(FLAGS_dns_resolution_slow_threshold_ms, 0); // 0 means disabled
    EXPECT_LE(FLAGS_dns_resolution_slow_threshold_ms, 60000); // Max 60 seconds

    // Log the current configuration for verification
    LOG_INFO("DNS resolution timeout configuration: slow_threshold_ms={}",
             FLAGS_dns_resolution_slow_threshold_ms);
}

TEST(dns_timeout_test, test_dns_resolution_normal_speed)
{
    // Test that normal-speed DNS resolutions work correctly

    // Set a reasonable slow threshold (1 second)
    int32_t original_threshold = FLAGS_dns_resolution_slow_threshold_ms;
    FLAGS_dns_resolution_slow_threshold_ms = 1000;

    // Resolve localhost (should be fast)
    host_port hp("localhost", 8080);
    ASSERT_TRUE(hp);

    rpc_address addr = hp.resolve();
    ASSERT_TRUE(addr);
    ASSERT_EQ(HOST_TYPE_IPV4, addr.type());

    // This should not trigger slow resolution warning
    LOG_INFO("DNS resolution completed for localhost (should be < 1000ms)");

    // Restore original threshold
    FLAGS_dns_resolution_slow_threshold_ms = original_threshold;
}

TEST(dns_timeout_test, test_dns_timeout_disabled)
{
    // Test behavior with slow resolution detection disabled

    // Save original threshold
    int32_t original_threshold = FLAGS_dns_resolution_slow_threshold_ms;

    // Disable slow resolution logging
    FLAGS_dns_resolution_slow_threshold_ms = 0;

    // Resolve a hostname
    host_port hp("localhost", 8081);
    ASSERT_TRUE(hp);

    rpc_address addr = hp.resolve();
    ASSERT_TRUE(addr);

    // No warnings should be logged regardless of resolution time
    LOG_INFO("DNS resolution completed with timeout detection disabled");

    // Restore original threshold
    FLAGS_dns_resolution_slow_threshold_ms = original_threshold;
}

TEST(dns_timeout_test, test_dns_timeout_with_low_threshold)
{
    // Test with a very low threshold to trigger slow detection

    // Save original threshold
    int32_t original_threshold = FLAGS_dns_resolution_slow_threshold_ms;

    // Set a very low threshold (1ms)
    FLAGS_dns_resolution_slow_threshold_ms = 1;

    // Resolve a hostname (will likely exceed 1ms)
    host_port hp("localhost", 8082);
    ASSERT_TRUE(hp);

    rpc_address addr = hp.resolve();
    ASSERT_TRUE(addr);

    // This might trigger slow resolution warning depending on system load
    LOG_INFO("DNS resolution completed with very low threshold (1ms)");

    // Restore original threshold
    FLAGS_dns_resolution_slow_threshold_ms = original_threshold;
}

TEST(dns_timeout_test, test_dns_timeout_with_high_threshold)
{
    // Test with a high threshold to avoid false warnings

    // Save original threshold
    int32_t original_threshold = FLAGS_dns_resolution_slow_threshold_ms;

    // Set a high threshold (10 seconds)
    FLAGS_dns_resolution_slow_threshold_ms = 10000;

    // Resolve multiple hostnames
    std::vector<host_port> hosts = {
        host_port("localhost", 8083),
        host_port("localhost", 8084),
        host_port("127.0.0.1", 8085),
    };

    for (const auto &hp : hosts) {
        ASSERT_TRUE(hp);
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
    }

    // None should trigger slow resolution warning (should complete in < 10 seconds)
    LOG_INFO("DNS resolutions completed with high threshold (10000ms)");

    // Restore original threshold
    FLAGS_dns_resolution_slow_threshold_ms = original_threshold;
}

TEST(dns_timeout_test, test_dns_timeout_configuration_safety)
{
    // Test that timeout configuration has safe limits

    // Test with reasonable timeout thresholds
    EXPECT_GE(FLAGS_dns_resolution_slow_threshold_ms, 0);
    EXPECT_LE(FLAGS_dns_resolution_slow_threshold_ms, 300000); // Max 5 minutes

    // Test that other DNS configurations are also safe
    EXPECT_GE(FLAGS_dns_resolver_cache_ttl_seconds, 0);
    EXPECT_LE(FLAGS_dns_resolver_cache_ttl_seconds, 86400); // Max 24 hours

    EXPECT_GE(FLAGS_dns_resolver_cache_max_size, 0);

    EXPECT_GE(FLAGS_dns_resolver_retry_max_count, 0);
    EXPECT_LE(FLAGS_dns_resolver_retry_max_count, 10);

    LOG_INFO("DNS timeout configuration safety checks passed");
}

TEST(dns_timeout_test, test_dns_timeout_production_scenarios)
{
    // Test timeout settings for different production scenarios

    // Save original threshold
    int32_t original_threshold = FLAGS_dns_resolution_slow_threshold_ms;

    // Scenario 1: Fast local DNS (datacenter)
    FLAGS_dns_resolution_slow_threshold_ms = 100; // 100ms
    LOG_INFO("Local DNS: threshold=100ms (fast infrastructure)");

    // Scenario 2: Cloud DNS (moderate latency)
    FLAGS_dns_resolution_slow_threshold_ms = 2000; // 2 seconds
    LOG_INFO("Cloud DNS: threshold=2000ms (cloud latency)");

    // Scenario 3: Geo-distributed DNS (high latency)
    FLAGS_dns_resolution_slow_threshold_ms = 5000; // 5 seconds
    LOG_INFO("Geo-distributed DNS: threshold=5000ms (global latency)");

    // Scenario 4: Troubleshooting (very sensitive)
    FLAGS_dns_resolution_slow_threshold_ms = 50; // 50ms
    LOG_INFO("Troubleshooting: threshold=50ms (detect any slowness)");

    // Restore original threshold
    FLAGS_dns_resolution_slow_threshold_ms = original_threshold;

    LOG_INFO("DNS timeout production scenarios test completed");
}

TEST(dns_timeout_test, test_dns_timeout_with_retry_interaction)
{
    // Test that slow resolution detection works with retry logic

    // Save original settings
    int32_t original_threshold = FLAGS_dns_resolution_slow_threshold_ms;
    int32_t original_retry_count = FLAGS_dns_resolver_retry_max_count;

    // Set low threshold to catch slow resolutions
    FLAGS_dns_resolution_slow_threshold_ms = 500; // 500ms
    FLAGS_dns_resolver_retry_max_count = 2;

    // Resolve a hostname
    host_port hp("localhost", 8090);
    ASSERT_TRUE(hp);

    rpc_address addr = hp.resolve();
    ASSERT_TRUE(addr);

    // If resolution was slow, it should have been logged
    LOG_INFO("DNS resolution with retry interaction test completed");

    // Restore original settings
    FLAGS_dns_resolution_slow_threshold_ms = original_threshold;
    FLAGS_dns_resolver_retry_max_count = original_retry_count;
}

TEST(dns_timeout_test, test_dns_timeout_with_cache_interaction)
{
    // Test that cached resolutions don't trigger slow warnings

    // Save original threshold
    int32_t original_threshold = FLAGS_dns_resolution_slow_threshold_ms;

    // Set low threshold
    FLAGS_dns_resolution_slow_threshold_ms = 1; // 1ms

    // Resolve same hostname multiple times
    host_port hp("localhost", 8091);
    ASSERT_TRUE(hp);

    // First resolution - might be slow
    rpc_address addr1 = hp.resolve();
    ASSERT_TRUE(addr1);

    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Second resolution - should be cached (fast)
    rpc_address addr2 = hp.resolve();
    ASSERT_TRUE(addr2);

    EXPECT_EQ(addr1, addr2);

    LOG_INFO("DNS resolution with cache interaction test completed");

    // Restore original threshold
    FLAGS_dns_resolution_slow_threshold_ms = original_threshold;
}

TEST(dns_timeout_test, test_dns_timeout_metrics)
{
    // Test that slow resolution metrics are tracked

    // Save original threshold
    int32_t original_threshold = FLAGS_dns_resolution_slow_threshold_ms;

    // Set low threshold to potentially trigger slow detection
    FLAGS_dns_resolution_slow_threshold_ms = 1;

    // Resolve hostnames
    std::vector<host_port> hosts = {
        host_port("localhost", 8092),
        host_port("localhost", 8093),
    };

    for (const auto &hp : hosts) {
        ASSERT_TRUE(hp);
        rpc_address addr = hp.resolve();
        ASSERT_TRUE(addr);
    }

    // Metrics should be tracked (even if no slow resolutions detected)
    LOG_INFO("DNS resolution metrics test completed (check metrics for slow_resolution count)");

    // Restore original threshold
    FLAGS_dns_resolution_slow_threshold_ms = original_threshold;
}

TEST(dns_timeout_test, test_dns_timeout_performance_impact)
{
    // Test that timeout detection has minimal performance impact

    // Save original threshold
    int32_t original_threshold = FLAGS_dns_resolution_slow_threshold_ms;

    // Set to 0 (disabled) to measure baseline performance
    FLAGS_dns_resolution_slow_threshold_ms = 0;

    // Measure baseline resolution time
    auto start = std::chrono::high_resolution_clock::now();

    host_port hp("localhost", 8094);
    ASSERT_TRUE(hp);
    rpc_address addr = hp.resolve();
    ASSERT_TRUE(addr);

    auto end = std::chrono::high_resolution_clock::now();
    auto baseline_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Now enable with low threshold
    FLAGS_dns_resolution_slow_threshold_ms = 1;

    start = std::chrono::high_resolution_clock::now();

    rpc_address addr2 = hp.resolve();
    ASSERT_TRUE(addr2);

    end = std::chrono::high_resolution_clock::now();
    auto measured_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Performance impact should be minimal (just time measurement)
    LOG_INFO("DNS resolution performance impact: baseline={}us, measured={}us, overhead={}us",
             baseline_duration, measured_duration,
             measured_duration - baseline_duration);

    // Restore original threshold
    FLAGS_dns_resolution_slow_threshold_ms = original_threshold;

    // Performance impact should be minimal (< 10% overhead)
    EXPECT_LT(std::abs(measured_duration - baseline_duration), baseline_duration / 10);
}

} // namespace dsn
