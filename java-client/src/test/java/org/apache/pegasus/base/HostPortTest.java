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
package org.apache.pegasus.base;

import static org.junit.jupiter.api.Assertions.*;

import org.apache.pegasus.client.PException;
import org.apache.pegasus.util.HostPortResolver;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class HostPortTest {

  @BeforeEach
  public void setUp() {
    HostPortResolver.resetForTest();
    HostPortResolver.initialize();
  }

  @AfterEach
  public void tearDown() {
    HostPortResolver.resetForTest();
  }

  @Test
  public void testNewHostPort() {
    host_port hp = new host_port();

    assertNull(hp.getHost());
    assertEquals(0, hp.getPort());
    assertEquals(0, hp.getHostPortType());
    assertFalse(hp.isValid());
  }

  @Test
  public void testIsValid() {
    host_port hp = new host_port();
    assertFalse(hp.isValid());

    hp.setHost("localhost");
    assertFalse(hp.isValid()); // port not set

    hp.setPort((short) 34801);
    assertTrue(hp.isValid());
  }

  @Test
  public void testToHostPortString() {
    host_port hp = new host_port();
    hp.setHost("localhost");
    hp.setPort((short) 34801);

    assertEquals("localhost:34801", hp.toHostPortString());
  }

  @Test
  public void testResolve() throws PException {
    host_port hp = new host_port();
    hp.setHost("localhost");
    hp.setPort((short) 34801);

    rpc_address addr = hp.resolve();

    assertNotNull(addr);
    assertFalse(addr.isInvalid());
    assertEquals(34801, addr.get_port());
  }

  @Test
  public void testResolveInvalid() {
    host_port hp = new host_port();
    hp.setHost("invalid.host.that.does.not.exist");
    hp.setPort((short) 34801);

    assertThrows(
        PException.class,
        () -> {
          hp.resolve();
        });
  }

  @Test
  public void testFromAddress() {
    rpc_address addr = new rpc_address();
    // Use 127.0.0.1:34801
    int ip = 0x7F000001; // 127.0.0.1
    addr.address = ((long) ip << 32) + ((long) 34801 << 16) + 1;

    host_port hp = host_port.fromAddress(addr);

    assertNotNull(hp);
    assertTrue(hp.isValid());
    assertEquals(34801, hp.getPort());
  }

  @Test
  public void testFromAddressInvalid() {
    host_port hp = host_port.fromAddress(null);
    assertNull(hp);

    rpc_address addr = new rpc_address();
    addr.address = 0; // invalid
    hp = host_port.fromAddress(addr);
    assertNull(hp);
  }

  @Test
  public void testSettersAndGetters() {
    host_port hp = new host_port();

    hp.setHost("example.com");
    assertEquals("example.com", hp.getHost());

    hp.setPort(8080);
    assertEquals(8080, hp.getPort());

    hp.setHostPortType((byte) 2);
    assertEquals(2, hp.getHostPortType());
  }

  @Test
  public void testEquals() {
    host_port hp1 = new host_port();
    hp1.setHost("localhost");
    hp1.setPort((short) 34801);
    hp1.setHostPortType((byte) 1);

    host_port hp2 = new host_port();
    hp2.setHost("localhost");
    hp2.setPort((short) 34801);
    hp2.setHostPortType((byte) 1);

    assertEquals(hp1, hp2);
  }

  @Test
  public void testNotEquals() {
    host_port hp1 = new host_port();
    hp1.setHost("localhost");
    hp1.setPort((short) 34801);

    host_port hp2 = new host_port();
    hp2.setHost("localhost");
    hp2.setPort((short) 34802);

    assertNotEquals(hp1, hp2);
  }

  @Test
  public void testHashCode() {
    host_port hp1 = new host_port();
    hp1.setHost("localhost");
    hp1.setPort((short) 34801);
    hp1.setHostPortType((byte) 1);

    host_port hp2 = new host_port();
    hp2.setHost("localhost");
    hp2.setPort((short) 34801);
    hp2.setHostPortType((byte) 1);

    assertEquals(hp1.hashCode(), hp2.hashCode());
  }
}
