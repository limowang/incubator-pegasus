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
package org.apache.pegasus.dns;

import static org.junit.jupiter.api.Assertions.*;

import org.apache.pegasus.base.rpc_address;
import org.apache.pegasus.client.PException;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class DnsResolverTest {

  private DnsResolver resolver;

  @BeforeEach
  public void setUp() {
    resolver = new DnsResolver(60000, 100); // 1min TTL, max 100 entries
  }

  @AfterEach
  public void tearDown() {
    if (resolver != null) {
      resolver.shutdown();
    }
  }

  @Test
  public void testResolveLocalhost() throws PException {
    rpc_address addr = resolver.resolve("localhost", 34801);

    assertNotNull(addr);
    assertFalse(addr.isInvalid());
    assertEquals(34801, addr.get_port());
  }

  @Test
  public void testResolveValidHostname() throws PException {
    // Use 127.0.0.1 which should resolve to localhost
    rpc_address addr = resolver.resolve("127.0.0.1", 8080);

    assertNotNull(addr);
    assertFalse(addr.isInvalid());
    assertEquals(8080, addr.get_port());
  }

  @Test
  public void testResolveInvalidHostname() {
    assertThrows(
        PException.class,
        () -> {
          resolver.resolve("invalid.hostname.that.does.not.exist.12345", 8080);
        });
  }

  @Test
  public void testResolveNullHostname() {
    assertThrows(
        PException.class,
        () -> {
          resolver.resolve(null, 8080);
        });
  }

  @Test
  public void testResolveEmptyHostname() {
    assertThrows(
        PException.class,
        () -> {
          resolver.resolve("", 8080);
        });
  }

  @Test
  public void testResolveInvalidPort() {
    assertThrows(
        PException.class,
        () -> {
          resolver.resolve("localhost", 0);
        });

    assertThrows(
        PException.class,
        () -> {
          resolver.resolve("localhost", -1);
        });

    assertThrows(
        PException.class,
        () -> {
          resolver.resolve("localhost", 65536);
        });
  }

  @Test
  public void testCaching() throws PException {
    // First resolution
    rpc_address addr1 = resolver.resolve("localhost", 34801);
    assertNotNull(addr1);

    // Check metrics
    assertEquals(1, resolver.getMonitor().getCacheMisses());

    // Second resolution (should be cached)
    rpc_address addr2 = resolver.resolve("localhost", 34801);

    // Both should return the same address
    assertEquals(addr1.address, addr2.address);

    // Check cache hit
    assertTrue(resolver.getMonitor().getCacheHits() >= 1);
  }

  @Test
  public void testCacheEviction() throws PException {
    // Create a small cache
    DnsResolver smallResolver = new DnsResolver(60000, 2);

    try {
      // Add 3 entries using localhost with different ports (should evict the first)
      smallResolver.resolve("localhost", 34801);
      smallResolver.resolve("localhost", 34802);
      smallResolver.resolve("localhost", 34803);

      // Monitor should show cache activity
      assertTrue(smallResolver.getMonitor().getCacheMisses() >= 3);

    } finally {
      smallResolver.shutdown();
    }
  }

  @Test
  public void testClearCache() throws PException {
    // Resolve and cache
    resolver.resolve("localhost", 34801);
    resolver.resolve("127.0.0.1", 34802);

    // Clear cache
    resolver.clearCache();
    assertEquals(0, resolver.getCache().size());

    // Resolve again should be cache miss
    resolver.resolve("localhost", 34801);
    assertTrue(resolver.getMonitor().getCacheMisses() > 0);
  }

  @Test
  public void testGetMonitor() {
    assertNotNull(resolver.getMonitor());
    assertNotNull(resolver.getCache());
  }

  @Test
  public void testResolveHostPort() throws PException {
    org.apache.pegasus.base.host_port hp = new org.apache.pegasus.base.host_port();
    hp.setHost("localhost");
    hp.setPort((short) 34801);

    rpc_address addr = resolver.resolve(hp);

    assertNotNull(addr);
    assertFalse(addr.isInvalid());
    assertEquals(34801, addr.get_port());
  }

  @Test
  public void testResolveInvalidHostPort() {
    org.apache.pegasus.base.host_port hp = new org.apache.pegasus.base.host_port();
    // Don't set host or port

    assertThrows(
        PException.class,
        () -> {
          resolver.resolve(hp);
        });
  }
}
