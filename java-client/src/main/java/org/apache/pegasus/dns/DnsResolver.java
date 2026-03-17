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

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.apache.pegasus.base.rpc_address;
import org.apache.pegasus.client.PException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * DNS resolver for resolving hostnames to IP addresses. Provides caching and monitoring
 * capabilities.
 */
public class DnsResolver {
  private static final Logger logger = LoggerFactory.getLogger(DnsResolver.class);

  private final DnsCache cache;
  private final DnsMonitor monitor;

  /**
   * Create a DNS resolver with specified cache configuration.
   *
   * @param cacheTTLMillis cache TTL in milliseconds
   * @param maxCacheSize maximum cache size
   */
  public DnsResolver(long cacheTTLMillis, int maxCacheSize) {
    this.cache = new DnsCache(cacheTTLMillis, maxCacheSize);
    this.monitor = new DnsMonitor();
    logger.info("DnsResolver initialized: cacheTTL={}ms, maxSize={}", cacheTTLMillis, maxCacheSize);
  }

  /**
   * Resolve a hostname to IP address.
   *
   * @param hostname the hostname to resolve
   * @param port the port number
   * @return resolved rpc_address containing IP and port
   * @throws PException if resolution fails
   */
  public rpc_address resolve(String hostname, int port) throws PException {
    // Validate inputs
    if (hostname == null || hostname.isEmpty()) {
      throw new PException("Invalid hostname: null or empty");
    }
    if (port <= 0 || port > 65535) {
      throw new PException("Invalid port: " + port + " (must be 1-65535)");
    }

    // Check cache first
    String cacheKey = hostname + ":" + port;
    DnsCache.CacheEntry cached = cache.get(cacheKey);
    if (cached != null) {
      monitor.recordCacheHit();
      logger.debug("DNS cache hit: {}", cacheKey);
      return cached.getAddress();
    }

    monitor.recordCacheMiss();
    logger.debug("DNS cache miss: {}, performing resolution", cacheKey);

    // Perform DNS resolution
    long startTime = System.currentTimeMillis();
    try {
      InetAddress[] addresses = InetAddress.getAllByName(hostname);

      // Use the first address
      InetAddress addr = addresses[0];
      byte[] byteArray = addr.getAddress();

      // Convert to integer IP
      int ip = ByteBuffer.wrap(byteArray).order(ByteOrder.BIG_ENDIAN).getInt();

      // Create rpc_address
      rpc_address result = new rpc_address();
      long addressValue = ((long) ip << 32) + ((long) port << 16) + 1;
      result.address = addressValue;

      // Cache the result
      cache.put(cacheKey, result);

      long timeTaken = System.currentTimeMillis() - startTime;
      monitor.recordResolutionSuccess(hostname, timeTaken);
      logger.debug("DNS resolved: {} -> {} ({}ms)", hostname, result, timeTaken);

      return result;

    } catch (UnknownHostException e) {
      long timeTaken = System.currentTimeMillis() - startTime;
      monitor.recordResolutionFailure(hostname);
      logger.error("Failed to resolve hostname: {} ({}ms)", hostname, timeTaken, e);
      throw new PException("Failed to resolve hostname: " + hostname, e);
    }
  }

  /**
   * Resolve host_port to rpc_address.
   *
   * @param hp the host_port object
   * @return resolved rpc_address
   * @throws PException if resolution fails or hp is invalid
   */
  public rpc_address resolve(org.apache.pegasus.base.host_port hp) throws PException {
    if (hp == null || !hp.isValid()) {
      throw new PException("Invalid host_port: " + hp);
    }
    return resolve(hp.getHost(), hp.getPort());
  }

  /** Get the monitor instance. */
  public DnsMonitor getMonitor() {
    return monitor;
  }

  /** Get the cache instance. */
  public DnsCache getCache() {
    return cache;
  }

  /** Clear the DNS cache. */
  public void clearCache() {
    cache.clear();
    logger.info("DNS cache cleared");
  }

  /** Shutdown the resolver and cleanup resources. */
  public void shutdown() {
    cache.shutdown();
    monitor.shutdown();
    logger.info("DnsResolver shutdown complete");
  }
}
