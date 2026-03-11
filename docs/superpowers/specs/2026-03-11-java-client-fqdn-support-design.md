# Java Client FQDN Support Design

**Date:** 2026-03-11
**Author:** Claude
**Status:** Approved

## Overview

This document describes the design for adding FQDN (Fully Qualified Domain Name) support to the Pegasus Java client, enabling it to connect to replica servers using domain names instead of just IP addresses.

## Background

The C++ implementation already has full FQDN support with `host_port` class, DNS resolution, and caching. The Java client has a `host_port` class but it's not integrated into the communication layer. Currently, the Java client only uses `rpc_address` (a 64-bit integer representing IP + port), which loses the original FQDN hostname.

## Goals

1. Enable Java client to connect to replica servers using FQDN from meta server responses
2. Support backward compatibility with meta servers that don't return `host_port` fields
3. Re-resolve FQDN on connection failures to support IP address changes
4. Minimize code changes and maintain existing architecture

## Non-Goals

1. Full DNS cache management with TTL (deferred to future iteration)
2. Runtime DNS cache invalidation commands (deferred to future iteration)
3. Replacing `rpc_address` as the primary address type

## Architecture

### Data Flow

```
Meta Server Response (partition_configuration)
        ↓
    TableHandler.initTableConfiguration()
        ↓
    Check hp_primary/hp_secondaries fields
        ↓
    ├─ Exists → Use host_port
    └─ Missing → Use rpc_address (backward compatible)
        ↓
    ClusterManager.getReplicaSession(addr, hostPort)
        ↓
    ReplicaSession (stores both address and hostPort)
        ↓
    Connection Failure → Re-resolve FQDN
        ↓
    Update session address and reconnect
```

### Key Components

#### 1. ReplicaSession

**Purpose:** Manage connection to a single replica server with FQDN support

**Changes:**
- Add field `host_port hostPort` to store original FQDN
- Add fields `String lastResolvedHost`, `int lastResolvedPort` for tracking
- Modify constructor to accept `host_port` parameter
- Add `resolveAndUpdateAddress()` method for FQDN re-resolution
- Modify `tryConnect()` to trigger re-resolution on failures

**Key Method:**
```java
private void resolveAndUpdateAddress() {
    if (hostPort == null) return;
    try {
        rpc_address newAddr = resolveHostPort(hostPort);
        if (!newAddr.equals(address)) {
            logger.info("FQDN resolved to new IP: {} -> {}", address, newAddr);
            manager_.updateReplicaSessionKey(this);
            this.address = newAddr;
        }
    } catch (Exception e) {
        logger.error("Failed to resolve FQDN: {}", hostPort, e);
    }
}
```

#### 2. ClusterManager

**Purpose:** Manage replica session pool with FQDN support

**Changes:**
- Overload `getReplicaSession(rpc_address, host_port)` to accept both address types
- Add `updateReplicaSessionKey(ReplicaSession)` method to handle FQDN re-resolution
- Maintain `ConcurrentHashMap<rpc_address, ReplicaSession>` as before

**Key Method:**
```java
public ReplicaSession getReplicaSession(rpc_address address, host_port hostPort) {
    // Use hostPort if available, fallback to address-only
    // Update existing session with hostPort if it was missing
}
```

#### 3. TableHandler

**Purpose:** Parse meta server configuration and create replica sessions

**Changes:**
- Modify `initTableConfiguration()` to check `hp_primary` and `hp_secondaries` fields
- Add helper method `resolveHostPortToRpcAddress(host_port)` for FQDN resolution
- Prefer `host_port` over `rpc_address` when both are available
- Pass `host_port` to `ClusterManager.getReplicaSession()`

**Key Logic:**
```java
if (pConfig.isSetHp_primary() && pConfig.hp_primary.getHost() != null) {
    // Use host_port
    rConfig.primarySession = manager_.getReplicaSession(resolvedAddr, hpPrimary);
} else {
    // Use rpc_address (backward compatible)
    rConfig.primarySession = manager_.getReplicaSession(pConfig.primary, null);
}
```

## Implementation Details

### Phase 1: Core Changes

1. **ReplicaSession Modifications**
   - Add `host_port` field and constructor overload
   - Implement `resolveAndUpdateAddress()` method
   - Modify `tryConnect()` to check and re-resolve FQDN on failures
   - Add logging for FQDN resolution events

2. **ClusterManager Modifications**
   - Add `getReplicaSession(rpc_address, host_port)` overload
   - Implement `updateReplicaSessionKey(ReplicaSession)` method
   - Update session creation to pass `host_port` parameter

3. **TableHandler Modifications**
   - Update `initTableConfiguration()` to parse `host_port` fields
   - Add `resolveHostPortToRpcAddress()` helper method
   - Pass `host_port` to session creation calls

### Phase 2: Testing

1. **Unit Tests**
   - Test `ReplicaSession` FQDN re-resolution logic
   - Test `ClusterManager` session key updates
   - Test `TableHandler` configuration parsing with both `rpc_address` and `host_port`

2. **Integration Tests**
   - Test against meta server returning `host_port` fields
   - Test against meta server without `host_port` fields (backward compatibility)
   - Test connection failure and FQDN re-resolution scenarios

### Phase 3: Documentation

1. Update client configuration documentation
2. Add FQDN support notes to README
3. Document behavior for connection failures and re-resolution

## Backward Compatibility

The design maintains full backward compatibility:

1. **Meta Server Compatibility**
   - Checks `hp_primary != null && hp_primary.getHost() != null`
   - Falls back to `rpc_address` if `host_port` is not available
   - Works with both old and new meta servers

2. **API Compatibility**
   - Existing `getReplicaSession(rpc_address)` method unchanged
   - New overload is additive, not breaking
   - Client code using IP addresses continues to work

## Error Handling

1. **FQDN Resolution Failures**
   - Log error with host_port details
   - Keep existing `rpc_address` as fallback
   - Don't create new sessions if resolution fails

2. **Connection Failures**
   - Check if `host_port` is available before re-resolution
   - Limit re-resolution attempts to avoid infinite loops
   - Fall back to original behavior if re-resolution fails

## Performance Considerations

1. **DNS Resolution**
   - Initial resolution at session creation (already happens in `rpc_address.fromString()`)
   - Re-resolution only on connection failures (not on every request)
   - No TTL-based caching in this phase (can be added later)

2. **Session Management**
   - No overhead for sessions without `host_port` (IP-only connections)
   - Minimal memory overhead (one additional `host_port` object per session)
   - Map key updates are rare (only when FQDN resolves to different IP)

## Future Enhancements

1. **DNS Caching**
   - Add LRU cache for DNS resolution results
   - Implement TTL-based cache expiration
   - Add cache size limits

2. **Runtime Cache Management**
   - Add commands to clear DNS cache
   - Add commands to invalidate specific FQDN entries
   - Add statistics monitoring

3. **Advanced Re-resolution**
   - Exponential backoff for re-resolution attempts
   - Configurable re-resolution limits
   - Background re-resolution for proactive IP updates

## References

- C++ implementation: `src/rpc/rpc_host_port.h`, `src/rpc/dns_resolver.cpp`
- Go implementation: `go-client/idl/base/host_port.go`
- Current Java `host_port` class: `java-client/src/main/java/org/apache/pegasus/base/host_port.java`
- IDL definition: `idl/dsn.layer2.thrift`
