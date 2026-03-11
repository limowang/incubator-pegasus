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
import io.netty.channel.nio.NioEventLoopGroup;
import io.netty.channel.EventLoopGroup;
import org.apache.pegasus.base.host_port;
import org.apache.pegasus.base.rpc_address;
import org.apache.pegasus.client.ClientOptions;
import org.apache.pegasus.rpc.interceptor.ReplicaSessionInterceptorManager;
import org.junit.Test;

public class ReplicaSessionFQDNTest {

    @Test
    public void testReplicaSessionWithHostPort() {
        host_port hp = new host_port();
        hp.setHost("localhost");
        hp.setPort(34801);
        hp.setHostPortType((byte) 1);

        rpc_address addr = rpc_address.fromIpPort("127.0.0.1:34801");
        assertNotNull(addr);

        EventLoopGroup rpcGroup = new NioEventLoopGroup(1);
        EventLoopGroup timeoutTaskGroup = new NioEventLoopGroup(1);

        try {
            ReplicaSession session = new ReplicaSession(
                addr,
                hp,
                rpcGroup,
                timeoutTaskGroup,
                1000,
                60,
                new ReplicaSessionInterceptorManager(ClientOptions.createBuilder().build()),
                null // clusterManager can be null for this test
            );

            assertNotNull(session);
            assertNotNull(session.getHostPort());
            assertEquals("localhost", session.getHostPort().getHost());
            assertEquals(34801, session.getHostPort().getPort());
        } finally {
            rpcGroup.shutdownGracefully();
            timeoutTaskGroup.shutdownGracefully();
        }
    }

    @Test
    public void testReplicaSessionWithoutHostPort() {
        rpc_address addr = rpc_address.fromIpPort("127.0.0.1:34801");
        assertNotNull(addr);

        EventLoopGroup rpcGroup = new NioEventLoopGroup(1);
        EventLoopGroup timeoutTaskGroup = new NioEventLoopGroup(1);

        try {
            ReplicaSession session = new ReplicaSession(
                addr,
                null, // hostPort
                rpcGroup,
                timeoutTaskGroup,
                1000,
                60,
                new ReplicaSessionInterceptorManager(ClientOptions.createBuilder().build()),
                null
            );

            assertNotNull(session);
            assertNull(session.getHostPort());
        } finally {
            rpcGroup.shutdownGracefully();
            timeoutTaskGroup.shutdownGracefully();
        }
    }
}
