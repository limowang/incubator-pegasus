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

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class DnsMonitorTest {

  private DnsMonitor monitor;

  @BeforeEach
  public void setUp() {
    monitor = new DnsMonitor();
  }

  @AfterEach
  public void tearDown() {
    if (monitor != null) {
      monitor.shutdown();
    }
  }

  @Test
  public void testInitialState() {
    assertEquals(0, monitor.getCacheHits());
    assertEquals(0, monitor.getCacheMisses());
    assertEquals(0, monitor.getResolutionSuccesses());
    assertEquals(0, monitor.getResolutionFailures());
    assertEquals(0.0, monitor.getCacheHitRate());
    assertEquals(0.0, monitor.getAverageResolutionTimeMs());
  }

  @Test
  public void testRecordCacheHit() {
    monitor.recordCacheHit();
    assertEquals(1, monitor.getCacheHits());
    assertEquals(1.0, monitor.getCacheHitRate()); // 1 hit / 1 total = 100%
  }

  @Test
  public void testRecordCacheMiss() {
    monitor.recordCacheMiss();
    assertEquals(1, monitor.getCacheMisses());
  }

  @Test
  public void testCacheHitRate() {
    monitor.recordCacheHit();
    monitor.recordCacheMiss();
    assertEquals(0.5, monitor.getCacheHitRate(), 0.001);

    monitor.recordCacheHit();
    assertEquals(2.0 / 3.0, monitor.getCacheHitRate(), 0.001);
  }

  @Test
  public void testRecordResolutionSuccess() {
    monitor.recordResolutionSuccess("localhost", 50);
    assertEquals(1, monitor.getResolutionSuccesses());
    assertEquals(50.0, monitor.getAverageResolutionTimeMs());
  }

  @Test
  public void testRecordResolutionFailure() {
    monitor.recordResolutionFailure("invalid.host");
    assertEquals(1, monitor.getResolutionFailures());
  }

  @Test
  public void testSlowResolution() {
    monitor.recordResolutionSuccess("slow.host", 150);
    assertEquals(1, monitor.getSlowResolutionCount());

    monitor.recordResolutionSuccess("fast.host", 50);
    assertEquals(1, monitor.getSlowResolutionCount()); // Still 1
  }

  @Test
  public void testAverageResolutionTime() {
    monitor.recordResolutionSuccess("host1", 100);
    monitor.recordResolutionSuccess("host2", 200);
    assertEquals(150.0, monitor.getAverageResolutionTimeMs());
  }
}
