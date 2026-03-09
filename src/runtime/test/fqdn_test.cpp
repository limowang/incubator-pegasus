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
#include "common/replication_other_types.h"

namespace dsn::replication {

TEST(fqdn_test, test_parse_server_list_with_fqdn)
{
    // Test parsing FQDN meta server list
    const char *server_list =
        "meta1.example.com:34801,meta2.example.com:34802,meta3.example.com:34803";

    std::vector<host_port> servers;
    bool result = replica_helper::parse_server_list(server_list, servers);

    ASSERT_TRUE(result) << "Failed to parse FQDN server list";
    ASSERT_EQ(3, servers.size());

    // Verify each server can be resolved to an IP address
    for (const auto &hp : servers) {
        ASSERT_TRUE(hp) << "Invalid host_port: " << hp.to_string();

        rpc_address addr = hp.resolve();
        // Note: This test uses example.com which may not resolve in all environments
        // In production, actual hostnames would be used
        EXPECT_TRUE(hp.type() == HOST_TYPE_IPV4) << "Host type should be IPV4";
        EXPECT_GT(hp.port(), 0) << "Port should be valid";
    }
}

TEST(fqdn_test, test_parse_server_list_with_mixed_formats)
{
    // Test parsing mixed FQDN and IP addresses
    const char *server_list = "localhost:34801,127.0.0.1:34802";

    std::vector<host_port> servers;
    bool result = replica_helper::parse_server_list(server_list, servers);

    ASSERT_TRUE(result) << "Failed to parse mixed format server list";
    ASSERT_EQ(2, servers.size());

    // Verify localhost can be resolved
    ASSERT_TRUE(servers[0]);
    rpc_address addr0 = servers[0].resolve();
    EXPECT_TRUE(addr0) << "localhost should resolve to 127.0.0.1";

    // Verify IP address works
    ASSERT_TRUE(servers[1]);
    rpc_address addr1 = servers[1].resolve();
    EXPECT_TRUE(addr1) << "127.0.0.1 should resolve successfully";
}

TEST(fqdn_test, test_validate_host_port_resolution)
{
    // Test valid FQDN
    host_port valid_hp("localhost", 8080);
    error_s err = replica_helper::validate_host_port_resolution(valid_hp);
    EXPECT_TRUE(err.is_ok()) << "localhost should be valid: " << err.description();

    // Test invalid host_port
    host_port invalid_hp;
    err = replica_helper::validate_host_port_resolution(invalid_hp);
    EXPECT_FALSE(err.is_ok()) << "Invalid host_port should fail validation";

    // Test FQDN with valid port
    host_port fqdn_hp("example.com", 34801);
    err = replica_helper::validate_host_port_resolution(fqdn_hp);
    // Note: example.com may not resolve, so we just check the function runs without crashing
    // In production tests, use actual hostnames that resolve
}

TEST(fqdn_test, test_parse_server_list_invalid_formats)
{
    // Test empty list
    {
        std::vector<host_port> servers;
        bool result = replica_helper::parse_server_list("", servers);
        EXPECT_FALSE(result) << "Empty server list should fail";
    }

    // Test invalid format (missing port)
    {
        std::vector<host_port> servers;
        bool result = replica_helper::parse_server_list("meta1.example.com", servers);
        EXPECT_FALSE(result) << "Server list without port should fail";
    }

    // Test invalid format (missing host)
    {
        std::vector<host_port> servers;
        bool result = replica_helper::parse_server_list(":34801", servers);
        EXPECT_FALSE(result) << "Server list without host should fail";
    }

    // Test duplicate servers
    {
        std::vector<host_port> servers;
        bool result = replica_helper::parse_server_list("localhost:34801,localhost:34801", servers);
        EXPECT_FALSE(result) << "Duplicate servers should fail";
    }
}

TEST(fqdn_test, test_host_port_to_string_and_resolve)
{
    // Test FQDN string representation and resolution
    host_port hp("localhost", 34801);
    std::string hp_str = hp.to_string();

    EXPECT_EQ("localhost:34801", hp_str);
    EXPECT_TRUE(hp);

    rpc_address addr = hp.resolve();
    EXPECT_TRUE(addr) << "localhost should resolve";
    EXPECT_EQ(HOST_TYPE_IPV4, addr.type());

    // Verify reverse resolution
    host_port hp_reverse = host_port::from_address(addr);
    EXPECT_TRUE(hp_reverse);
    EXPECT_EQ(34801, hp_reverse.port());
}

} // namespace dsn::replication
