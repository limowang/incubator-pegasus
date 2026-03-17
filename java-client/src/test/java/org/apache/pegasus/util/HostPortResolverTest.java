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
package org.apache.pegasus.util;

import static org.junit.jupiter.api.Assertions.*;

import org.apache.pegasus.base.host_port;
import org.apache.pegasus.base.rpc_address;
import org.apache.pegasus.client.PException;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class HostPortResolverTest {

  @BeforeEach
  public void setUp() {
    HostPortResolver.resetForTest();
  }

  @AfterEach
  public void tearDown() {
    HostPortResolver.resetForTest();
  }

  @Test
  public void testInitialize() {
    assertFalse(HostPortResolver.isInitialized());

    HostPortResolver.initialize();

    assertTrue(HostPortResolver.isInitialized());
  }

  @Test
  public void testInitializeWithCustomParams() {
    HostPortResolver.initialize(10000, 500); // 10s TTL, 500 entries

    assertTrue(HostPortResolver.isInitialized());
  }

  @Test
  public void testDoubleInitialize() {
    HostPortResolver.initialize();
    assertTrue(HostPortResolver.isInitialized());

    // Second initialization should be ignored
    HostPortResolver.initialize();
    assertTrue(HostPortResolver.isInitialized());
  }

  @Test
  public void testResolveBeforeInit() {
    assertThrows(
        PException.class,
        () -> {
          host_port hp = new host_port();
          hp.setHost("localhost");
          hp.setPort((short) 34801);
          HostPortResolver.resolve(hp);
        });
  }

  @Test
  public void testResolve() throws PException {
    HostPortResolver.initialize();

    host_port hp = new host_port();
    hp.setHost("localhost");
    hp.setPort((short) 34801);

    rpc_address addr = HostPortResolver.resolve(hp);

    assertNotNull(addr);
    assertFalse(addr.isInvalid());
    assertEquals(34801, addr.get_port());
  }

  @Test
  public void testResolveInvalidHostPort() {
    HostPortResolver.initialize();

    host_port hp = new host_port();
    // Don't set host or port

    assertThrows(
        PException.class,
        () -> {
          HostPortResolver.resolve(hp);
        });
  }

  @Test
  public void testGetResolver() {
    HostPortResolver.initialize();
    assertNotNull(HostPortResolver.getResolver());
  }

  @Test
  public void testReset() {
    HostPortResolver.initialize();
    assertTrue(HostPortResolver.isInitialized());

    HostPortResolver.resetForTest();
    assertFalse(HostPortResolver.isInitialized());
    assertNull(HostPortResolver.getResolver());
  }

  @Test
  public void testResolveWithCaching() throws PException {
    HostPortResolver.initialize();

    host_port hp = new host_port();
    hp.setHost("localhost");
    hp.setPort((short) 34801);

    // First resolution
    rpc_address addr1 = HostPortResolver.resolve(hp);
    assertNotNull(addr1);

    // Check cache metrics
    assertTrue(HostPortResolver.getResolver().getMonitor().getCacheMisses() > 0);

    // Second resolution (should be cached)
    rpc_address addr2 = HostPortResolver.resolve(hp);
    assertEquals(addr1.address, addr2.address);

    // Check cache hit
    assertTrue(HostPortResolver.getResolver().getMonitor().getCacheHits() > 0);
  }
}
