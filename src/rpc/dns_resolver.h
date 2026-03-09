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

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "rpc/group_host_port.h"
#include "rpc/rpc_address.h"
#include "rpc/rpc_host_port.h"
#include "utils/errors.h"
#include "utils/metrics.h"
#include "utils/ports.h"
#include "utils/singleton.h"
#include "utils/synchronize.h"

namespace dsn {

// This class provide a way to resolve host_port to rpc_address.
// Now each host_post will be resolved just once, and then cached the first rpc_address result in
// the resolved result list.
// If some host_port's rpc_address changes, you need to restart the Pegasus process to make it take
// effect.
// TODO(yingchun): Now the cache is unlimited, the cache size may be huge. Implement an expiration
// mechanism to limit the cache size and make it possible to update the resolve result.
class dns_resolver : public utils::singleton<dns_resolver>
{
public:
    // Resolve comma separated host:port list 'host_ports' to comma separated ip:port list.
    static std::string ip_ports_from_host_ports(const std::string &host_ports);

private:
    dns_resolver();
    ~dns_resolver() = default;

    friend class utils::singleton<dns_resolver>;
    friend class host_port;

    // Resolve the host_port object 'hp' into an rpc_address(i.e. a group or an ip).
    rpc_address resolve_address(const host_port &hp);

    // Resolve the host_port group object into an rpc_address(i.e. a group).
    rpc_address resolve_address(const rpc_group_host_port &group);

    bool get_cached_addresses(const host_port &hp, std::vector<rpc_address> &addresses);

    error_s resolve_addresses(const host_port &hp, std::vector<rpc_address> &addresses);

    // Cache entry with timestamp for LRU eviction and TTL expiration
    struct cache_entry
    {
        rpc_address address;
        uint64_t last_access_time;
        uint64_t creation_time;

        cache_entry() : last_access_time(0), creation_time(0) {}

        explicit cache_entry(rpc_address addr) : address(std::move(addr)), last_access_time(0), creation_time(0)
        {
            update_access_time();
            creation_time = last_access_time;
        }

        void update_access_time() { last_access_time = dsn_now_ns(); }

        bool is_expired(uint64_t ttl_ns) const
        {
            if (ttl_ns == 0) {
                return false; // TTL of 0 means no expiration
            }
            return (dsn_now_ns() - creation_time) > ttl_ns;
        }
    };

    void evict_lru_if_needed();
    bool is_entry_expired(const cache_entry &entry) const;

    mutable utils::rw_lock_nr _lock;
    // Cache the host_port resolve results, the cached rpc_address is the first one in the resolved
    // list.
    std::unordered_map<host_port, cache_entry> _dns_cache;

    METRIC_VAR_DECLARE_gauge_int64(dns_resolver_cache_size);
    METRIC_VAR_DECLARE_percentile_int64(dns_resolver_resolve_duration_ns);
    METRIC_VAR_DECLARE_percentile_int64(dns_resolver_resolve_by_dns_duration_ns);
    METRIC_VAR_DECLARE_counter(dns_resolver_resolve_success);
    METRIC_VAR_DECLARE_counter(dns_resolver_resolve_failure);
    METRIC_VAR_DECLARE_counter(dns_resolver_cache_hit);
    METRIC_VAR_DECLARE_counter(dns_resolver_cache_miss);
    METRIC_VAR_DECLARE_counter(dns_resolver_cache_eviction);
    METRIC_VAR_DECLARE_counter(dns_resolver_cache_expired);

    DISALLOW_COPY_AND_ASSIGN(dns_resolver);
    DISALLOW_MOVE_AND_ASSIGN(dns_resolver);
};

} // namespace dsn
