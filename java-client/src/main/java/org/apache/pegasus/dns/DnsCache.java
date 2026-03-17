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

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import org.apache.pegasus.base.rpc_address;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** LRU cache for DNS resolution results with TTL support. */
public class DnsCache {
  private static final Logger logger = LoggerFactory.getLogger(DnsCache.class);

  private final long ttlMillis;
  private final int maxSize;
  private final LRUCache cache;
  private final ScheduledExecutorService cleanupExecutor;

  /**
   * Create a DNS cache with specified TTL and max size.
   *
   * @param ttlMillis time-to-live for cache entries in milliseconds
   * @param maxSize maximum number of entries in cache
   */
  public DnsCache(long ttlMillis, int maxSize) {
    this.ttlMillis = ttlMillis;
    this.maxSize = maxSize;
    this.cache = new LRUCache(maxSize);
    this.cleanupExecutor =
        Executors.newSingleThreadScheduledExecutor(
            r -> {
              Thread t = new Thread(r, "dns-cache-cleanup");
              t.setDaemon(true);
              return t;
            });

    // Schedule periodic cleanup of expired entries
    this.cleanupExecutor.scheduleAtFixedRate(
        this::cleanupExpired, ttlMillis / 2, ttlMillis / 2, TimeUnit.MILLISECONDS);

    logger.info("DNS cache initialized: maxSize={}, TTL={}ms", maxSize, ttlMillis);
  }

  /**
   * Get a cached entry.
   *
   * @param key cache key (format: "hostname:port")
   * @return cached address, or null if not found or expired
   */
  public CacheEntry get(String key) {
    synchronized (cache) {
      CacheEntry entry = cache.get(key);
      if (entry != null && entry.isExpired(ttlMillis)) {
        cache.remove(key);
        return null;
      }
      return entry;
    }
  }

  /**
   * Put an address into cache.
   *
   * @param key cache key
   * @param address resolved address
   */
  public void put(String key, rpc_address address) {
    synchronized (cache) {
      cache.put(key, new CacheEntry(address));
    }
  }

  /** Clear all cache entries. */
  public void clear() {
    synchronized (cache) {
      cache.clear();
    }
    logger.info("DNS cache cleared");
  }

  /** Get cache size. */
  public int size() {
    synchronized (cache) {
      return cache.size();
    }
  }

  /** Cleanup expired entries. */
  private void cleanupExpired() {
    synchronized (cache) {
      int removed = 0;
      for (Map.Entry<String, CacheEntry> entry : cache.entrySet()) {
        if (entry.getValue().isExpired(ttlMillis)) {
          cache.remove(entry.getKey());
          removed++;
        }
      }
      if (removed > 0) {
        logger.debug("Cleaned up {} expired DNS entries", removed);
      }
    }
  }

  /** Shutdown the cache and cleanup resources. */
  public void shutdown() {
    cleanupExecutor.shutdown();
    try {
      if (!cleanupExecutor.awaitTermination(5, TimeUnit.SECONDS)) {
        cleanupExecutor.shutdownNow();
      }
    } catch (InterruptedException e) {
      cleanupExecutor.shutdownNow();
      Thread.currentThread().interrupt();
    }
  }

  /** Cache entry containing resolved address and timestamp. */
  public static class CacheEntry {
    private final rpc_address address;
    private final long timestamp;

    public CacheEntry(rpc_address address) {
      this.address = address;
      this.timestamp = System.currentTimeMillis();
    }

    public rpc_address getAddress() {
      return address;
    }

    public long getTimestamp() {
      return timestamp;
    }

    public boolean isExpired(long ttlMillis) {
      return System.currentTimeMillis() - timestamp > ttlMillis;
    }
  }

  /** Simple LRU cache implementation using LinkedHashMap. */
  private class LRUCache extends LinkedHashMap<String, CacheEntry> {
    private final int maxCapacity;

    public LRUCache(int maxCapacity) {
      super(maxCapacity, 0.75f, true);
      this.maxCapacity = maxCapacity;
    }

    @Override
    protected boolean removeEldestEntry(Map.Entry<String, CacheEntry> eldest) {
      return this.size() > maxCapacity;
    }
  }
}
