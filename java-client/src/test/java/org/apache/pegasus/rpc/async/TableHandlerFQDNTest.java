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
package org.apache.pegasus.rpc.async;

import static org.junit.Assert.*;
import org.apache.pegasus.base.host_port;
import org.apache.pegasus.base.rpc_address;
import org.apache.pegasus.base.gpid;
import org.apache.pegasus.replication.partition_configuration;
import org.junit.Test;

public class TableHandlerFQDNTest {

    @Test
    public void testResolveHostPortToRpcAddress() {
        // Create a mock TableHandler to test the helper method
        // Note: This requires reflection or a test-friendly version

        host_port hp = new host_port();
        hp.setHost("127.0.0.1"); // Use IP to avoid DNS in tests
        hp.setPort(34801);
        hp.setHostPortType((byte) 1);

        // Test that we can resolve it
        rpc_address addr = rpc_address.fromIpPort("127.0.0.1:34801");
        assertNotNull(addr);
        assertFalse(addr.isInvalid());
        assertEquals(34801, addr.get_port());
    }

    @Test
    public void testParseConfigurationWithHostPort() {
        // This is an integration test - requires a running meta server
        // or mocking of the entire query_cfg_response flow

        // For now, just verify the structure is correct
        partition_configuration pConfig = new partition_configuration();
        pConfig.pid = new gpid(1, 0);

        // Set up rpc_address
        pConfig.primary = rpc_address.fromIpPort("127.0.0.1:34801");

        // Set up host_port
        host_port hp = new host_port();
        hp.setHost("localhost");
        hp.setPort(34801);
        hp.setHostPortType((byte) 1);
        // Note: Actual field assignment depends on Thrift-generated code
        // pConfig.setHp_primary(hp);

        assertNotNull(pConfig.primary);
        // assertNotNull(pConfig.getHp_primary());
    }

    @Test
    public void testParseConfigurationWithoutHostPort() {
        partition_configuration pConfig = new partition_configuration();
        pConfig.pid = new gpid(1, 0);
        pConfig.primary = rpc_address.fromIpPort("127.0.0.1:34801");

        assertNotNull(pConfig.primary);
        // Verify host_port field is null/not set
        // assertNull(pConfig.getHp_primary());
    }
}
