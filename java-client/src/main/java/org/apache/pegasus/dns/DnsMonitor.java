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

import java.util.concurrent.atomic.AtomicLong;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Monitor for DNS resolution metrics. Tracks cache hits, resolution success/failure, and
 * performance metrics.
 */
public class DnsMonitor {
  private static final Logger logger = LoggerFactory.getLogger(DnsMonitor.class);

  private final AtomicLong cacheHits = new AtomicLong(0);
  private final AtomicLong cacheMisses = new AtomicLong(0);
  private final AtomicLong resolutionSuccesses = new AtomicLong(0);
  private final AtomicLong resolutionFailures = new AtomicLong(0);

  private volatile long totalResolutionTimeMs = 0;
  private final AtomicLong slowResolutionCount = new AtomicLong(0);

  /** Record a cache hit event. */
  public void recordCacheHit() {
    cacheHits.incrementAndGet();
  }

  /** Record a cache miss event. */
  public void recordCacheMiss() {
    cacheMisses.incrementAndGet();
  }

  /**
   * Record a successful DNS resolution.
   *
   * @param hostname the hostname that was resolved
   * @param timeTakenMs the time taken for resolution in milliseconds
   */
  public void recordResolutionSuccess(String hostname, long timeTakenMs) {
    resolutionSuccesses.incrementAndGet();
    totalResolutionTimeMs += timeTakenMs;

    if (timeTakenMs > 100) {
      slowResolutionCount.incrementAndGet();
      logger.warn("Slow DNS resolution: {} took {}ms", hostname, timeTakenMs);
    }
  }

  /**
   * Record a failed DNS resolution.
   *
   * @param hostname the hostname that failed to resolve
   */
  public void recordResolutionFailure(String hostname) {
    resolutionFailures.incrementAndGet();
    logger.error("DNS resolution failed for hostname: {}", hostname);
  }

  /** Get the number of cache hits. */
  public long getCacheHits() {
    return cacheHits.get();
  }

  /** Get the number of cache misses. */
  public long getCacheMisses() {
    return cacheMisses.get();
  }

  /** Get the number of successful resolutions. */
  public long getResolutionSuccesses() {
    return resolutionSuccesses.get();
  }

  /** Get the number of failed resolutions. */
  public long getResolutionFailures() {
    return resolutionFailures.get();
  }

  /**
   * Calculate the cache hit rate.
   *
   * @return cache hit rate between 0.0 and 1.0
   */
  public double getCacheHitRate() {
    long total = cacheHits.get() + cacheMisses.get();
    return total == 0 ? 0.0 : (double) cacheHits.get() / total;
  }

  /**
   * Calculate the average resolution time.
   *
   * @return average time in milliseconds
   */
  public double getAverageResolutionTimeMs() {
    long successes = resolutionSuccesses.get();
    return successes == 0 ? 0.0 : (double) totalResolutionTimeMs / successes;
  }

  /** Get the number of slow resolutions (>100ms). */
  public long getSlowResolutionCount() {
    return slowResolutionCount.get();
  }

  /** Log current metrics. */
  public void logMetrics() {
    logger.info(
        "DNS Metrics - cacheHitRate={:.2f}, successes={}, failures={}, avgTime={:.2f}ms, slowResolutions={}",
        getCacheHitRate(),
        getResolutionSuccesses(),
        getResolutionFailures(),
        getAverageResolutionTimeMs(),
        getSlowResolutionCount());
  }

  /** Shutdown the monitor and log final metrics. */
  public void shutdown() {
    logMetrics();
  }
}
