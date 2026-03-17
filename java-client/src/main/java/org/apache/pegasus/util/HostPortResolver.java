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

import java.util.concurrent.atomic.AtomicBoolean;
import org.apache.pegasus.base.host_port;
import org.apache.pegasus.base.rpc_address;
import org.apache.pegasus.client.PException;
import org.apache.pegasus.dns.DnsResolver;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Singleton utility class for resolving host_port to rpc_address. Should be initialized during
 * client startup.
 */
public class HostPortResolver {
  private static final Logger logger = LoggerFactory.getLogger(HostPortResolver.class);

  private static volatile DnsResolver resolver;
  private static final AtomicBoolean initialized = new AtomicBoolean(false);

  // Default configuration
  private static final long DEFAULT_CACHE_TTL_MILLIS = 5 * 60 * 1000; // 5 minutes
  private static final int DEFAULT_MAX_CACHE_SIZE = 1000;

  /** Private constructor to prevent instantiation. */
  private HostPortResolver() {
    // Utility class
  }

  /** Initialize the resolver with default settings. Should be called during client startup. */
  public static void initialize() {
    initialize(DEFAULT_CACHE_TTL_MILLIS, DEFAULT_MAX_CACHE_SIZE);
  }

  /**
   * Initialize the resolver with custom settings.
   *
   * @param cacheTTLMillis cache TTL in milliseconds
   * @param maxCacheSize maximum cache size
   */
  public static synchronized void initialize(long cacheTTLMillis, int maxCacheSize) {
    if (initialized.get()) {
      logger.warn("HostPortResolver already initialized, skipping");
      return;
    }

    resolver = new DnsResolver(cacheTTLMillis, maxCacheSize);
    initialized.set(true);

    logger.info(
        "HostPortResolver initialized: cacheTTL={}ms, maxSize={}", cacheTTLMillis, maxCacheSize);

    // Register shutdown hook
    Runtime.getRuntime()
        .addShutdownHook(
            new Thread(
                () -> {
                  if (resolver != null) {
                    resolver.shutdown();
                  }
                },
                "dns-resolver-shutdown"));
  }

  /**
   * Resolve a host_port to rpc_address.
   *
   * @param hp the host_port to resolve
   * @return resolved rpc_address
   * @throws PException if resolution fails or resolver not initialized
   */
  public static rpc_address resolve(host_port hp) throws PException {
    if (!initialized.get()) {
      throw new PException("HostPortResolver not initialized. Call initialize() first.");
    }

    if (hp == null || !hp.isValid()) {
      throw new PException("Invalid host_port: " + hp);
    }

    return resolver.resolve(hp);
  }

  /**
   * Check if the resolver is initialized.
   *
   * @return true if initialized, false otherwise
   */
  public static boolean isInitialized() {
    return initialized.get();
  }

  /**
   * Get the resolver instance (for testing/monitoring).
   *
   * @return the DnsResolver instance, or null if not initialized
   */
  public static DnsResolver getResolver() {
    return resolver;
  }

  /** Reset the resolver (for testing only). */
  public static void resetForTest() {
    initialized.set(false);
    resolver = null;
  }

  /** Shutdown the resolver (for cleanup). */
  public static void shutdown() {
    if (resolver != null) {
      resolver.shutdown();
    }
    initialized.set(false);
    resolver = null;
  }
}
