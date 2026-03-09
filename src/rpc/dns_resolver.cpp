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

#include <algorithm>
#include <chrono>
#include <memory>
#include <set>
#include <string_view>
#include <thread>
#include <utility>

#include "fmt/core.h"
#include "fmt/format.h"
#include "rpc/dns_resolver.h"
#include "rpc/group_address.h"
#include "utils/autoref_ptr.h"
#include "utils/fmt_logging.h"
#include "utils/ports.h"
#include "utils/strings.h"

METRIC_DEFINE_gauge_int64(server,
                          dns_resolver_cache_size,
                          dsn::metric_unit::kKeys,
                          "The size of the host_port to rpc_address resolve results cache");

METRIC_DEFINE_percentile_int64(
    server,
    dns_resolver_resolve_duration_ns,
    dsn::metric_unit::kNanoSeconds,
    "The duration of resolving a host port, may either get from cache or resolve by DNS lookup");

METRIC_DEFINE_percentile_int64(server,
                               dns_resolver_resolve_by_dns_duration_ns,
                               dsn::metric_unit::kNanoSeconds,
                               "The duration of resolving a host port by DNS lookup");

METRIC_DEFINE_counter(server,
                     dns_resolver_resolve_success,
                     dsn::metric_unit::kResolves,
                     "The number of successful host_port to rpc_address resolutions");

METRIC_DEFINE_counter(server,
                     dns_resolver_resolve_failure,
                     dsn::metric_unit::kResolves,
                     "The number of failed host_port to rpc_address resolutions");

METRIC_DEFINE_counter(server,
                     dns_resolver_cache_hit,
                     dsn::metric_unit::kResolves,
                     "The number of host_port resolutions served from cache");

METRIC_DEFINE_counter(server,
                     dns_resolver_cache_miss,
                     dsn::metric_unit::kResolves,
                     "The number of host_port resolutions that required DNS lookup");

METRIC_DEFINE_counter(server,
                     dns_resolver_resolve_retry,
                     dsn::metric_unit::kResolves,
                     "The number of DNS resolution retry attempts");

namespace dsn {

DSN_DEFINE_int32(network,
                 dns_resolver_retry_max_count,
                 3,
                 "Maximum number of retry attempts for DNS resolution on transient failures");

DSN_DEFINE_int32(network,
                 dns_resolver_retry_delay_ms,
                 100,
                 "Initial delay in milliseconds between DNS resolution retry attempts "
                 "(doubles after each retry up to max 5000ms)");

dns_resolver::dns_resolver()
    : METRIC_VAR_INIT_server(dns_resolver_cache_size),
      METRIC_VAR_INIT_server(dns_resolver_resolve_duration_ns),
      METRIC_VAR_INIT_server(dns_resolver_resolve_by_dns_duration_ns),
      METRIC_VAR_INIT_server(dns_resolver_resolve_success),
      METRIC_VAR_INIT_server(dns_resolver_resolve_failure),
      METRIC_VAR_INIT_server(dns_resolver_cache_hit),
      METRIC_VAR_INIT_server(dns_resolver_cache_miss)
{
#ifndef MOCK_TEST
    static int only_one_instance = 0;
    ++only_one_instance;
    CHECK_EQ_MSG(1, only_one_instance, "dns_resolver should only created once!");
#endif
}

bool dns_resolver::get_cached_addresses(const host_port &hp, std::vector<rpc_address> &addresses)
{
    utils::auto_read_lock l(_lock);
    const auto &found = _dns_cache.find(hp);
    if (found == _dns_cache.end()) {
        return false;
    }

    addresses = {found->second};
    METRIC_VAR_INCREMENT(dns_resolver_cache_hit);
    return true;
}

error_s dns_resolver::resolve_addresses(const host_port &hp, std::vector<rpc_address> &addresses)
{
    CHECK(addresses.empty(), "invalid addresses, not empty");
    if (get_cached_addresses(hp, addresses)) {
        METRIC_VAR_INCREMENT(dns_resolver_resolve_success);
        return error_s::ok();
    }

    METRIC_VAR_INCREMENT(dns_resolver_cache_miss);

    // Retry logic for DNS resolution with exponential backoff
    int retry_count = 0;
    int delay_ms = FLAGS_dns_resolver_retry_delay_ms;
    error_s last_err;

    while (retry_count <= FLAGS_dns_resolver_retry_max_count) {
        std::vector<rpc_address> resolved_addresses;
        {
            METRIC_VAR_AUTO_LATENCY(dns_resolver_resolve_by_dns_duration_ns);
            last_err = hp.resolve_addresses(resolved_addresses);
        }

        if (last_err) {
            // Success
            {
                if (resolved_addresses.size() > 1) {
                    LOG_DEBUG("host_port '{}' resolves to {} different addresses {}, only the first one {} "
                              "will be cached.",
                              hp,
                              resolved_addresses.size(),
                              fmt::join(resolved_addresses, ","),
                              resolved_addresses[0]);
                }

                utils::auto_write_lock l(_lock);
                const auto it = _dns_cache.insert(std::make_pair(hp, resolved_addresses[0]));
                if (it.second) {
                    METRIC_VAR_INCREMENT(dns_resolver_cache_size);
                }
            }

            addresses = std::move(resolved_addresses);
            METRIC_VAR_INCREMENT(dns_resolver_resolve_success);

            if (retry_count > 0) {
                LOG_INFO("DNS resolution succeeded for '{}' after {} retry attempt(s)",
                         hp,
                         retry_count);
            }
            return error_s::ok();
        }

        // Check if error is transient (worth retrying)
        if (retry_count < FLAGS_dns_resolver_retry_max_count) {
            METRIC_VAR_INCREMENT(dns_resolver_resolve_retry);
            LOG_WARNING("DNS resolution failed for '{}' (attempt {}/{}): {}, retrying in {}ms...",
                         hp,
                         retry_count + 1,
                         FLAGS_dns_resolver_retry_max_count + 1,
                         last_err.description(),
                         delay_ms);

            // Sleep before retry with exponential backoff
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

            // Exponential backoff: double the delay, capped at 5000ms
            delay_ms = std::min(delay_ms * 2, 5000);
            retry_count++;
        } else {
            break; // Max retries reached
        }
    }

    // All retries failed
    METRIC_VAR_INCREMENT(dns_resolver_resolve_failure);
    LOG_ERROR("Failed to resolve host_port '{}' after {} retry attempts: {}",
              hp,
              retry_count,
              last_err.description());
    return last_err;
}

// NOLINTNEXTLINE(misc-no-recursion)
rpc_address dns_resolver::resolve_address(const rpc_group_host_port &group)
{
    rpc_address addr;
    addr.assign_group(group.name());

    for (const auto &member : group.members()) {
        CHECK_TRUE(addr.group_address()->add(resolve_address(member)));
    }
    addr.group_address()->set_update_leader_automatically(group.is_update_leader_automatically());
    addr.group_address()->set_leader(resolve_address(group.leader()));
    return addr;
}

// NOLINTNEXTLINE(misc-no-recursion)
rpc_address dns_resolver::resolve_address(const host_port &hp)
{
    METRIC_VAR_AUTO_LATENCY(dns_resolver_resolve_duration_ns);
    switch (hp.type()) {
    case HOST_TYPE_GROUP:
        return resolve_address(*hp.group_host_port());
    case HOST_TYPE_IPV4: {
        std::vector<rpc_address> addresses;
        CHECK_OK(resolve_addresses(hp, addresses), "host_port '{}' can not be resolved", hp);
        CHECK(!addresses.empty(), "host_port '{}' can not be resolved to any address", hp);

        if (addresses.size() > 1) {
            LOG_WARNING("host_port '{}' resolves to {} different addresses, using the first one {}",
                        hp,
                        addresses.size(),
                        addresses[0]);
        }
        return addresses[0];
    }
    default:
        return {};
    }
}

std::string dns_resolver::ip_ports_from_host_ports(const std::string &host_ports)
{
    std::vector<std::string> host_port_vec;
    dsn::utils::split_args(host_ports.c_str(), host_port_vec, ',');

    if (dsn_unlikely(host_port_vec.empty())) {
        return host_ports;
    }

    std::vector<std::string> ip_port_vec;
    ip_port_vec.reserve(host_port_vec.size());
    for (const auto &hp : host_port_vec) {
        const auto addr = host_port::from_string(hp).resolve();
        ip_port_vec.emplace_back(addr.to_string());
    }

    return fmt::format("{}", fmt::join(ip_port_vec, ","));
}

} // namespace dsn
