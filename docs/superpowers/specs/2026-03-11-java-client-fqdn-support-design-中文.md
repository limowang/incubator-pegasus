# Java 客户端 FQDN 支持设计文档

**日期：** 2026-03-11
**作者：** Claude
**状态：** 已批准

## 概述

本文档描述了为 Pegasus Java 客户端添加 FQDN（完全限定域名）支持的设计方案，使其能够使用域名而不是仅使用 IP 地址连接到副本服务器。

## 背景

C++ 实现已经具有完整的 FQDN 支持，包括 `host_port` 类、DNS 解析和缓存。Java 客户端虽然有 `host_port` 类，但尚未集成到通信层中。目前，Java 客户端仅使用 `rpc_address`（一个表示 IP + 端口的 64 位整数），这会丢失原始的 FQDN 主机名。

## 目标

1. 使 Java 客户端能够使用 meta 服务器响应中的 FQDN 连接到副本服务器
2. 支持与不返回 `host_port` 字段的 meta 服务器的向后兼容性
3. 在连接失败时重新解析 FQDN 以支持 IP 地址变更
4. 最小化代码更改并保持现有架构

## 非目标

1. 完整的带 TTL 的 DNS 缓存管理（推迟到未来迭代）
2. 运行时 DNS 缓存失效命令（推迟到未来迭代）
3. 替换 `rpc_address` 作为主要地址类型

## 架构

### 数据流

```
Meta 服务器响应 (partition_configuration)
        ↓
    TableHandler.initTableConfiguration()
        ↓
    检查 hp_primary/hp_secondaries 字段
        ↓
    ├─ 存在 → 使用 host_port
    └─ 缺失 → 使用 rpc_address (向后兼容)
        ↓
    ClusterManager.getReplicaSession(addr, hostPort)
        ↓
    ReplicaSession (同时存储 address 和 hostPort)
        ↓
    连接失败 → 重新解析 FQDN
        ↓
    更新 session 地址并重新连接
```

### 核心组件

#### 1. ReplicaSession

**用途：** 管理到单个副本服务器的连接，支持 FQDN

**更改：**
- 添加字段 `host_port hostPort` 以存储原始 FQDN
- 添加字段 `String lastResolvedHost`、`int lastResolvedPort` 用于跟踪
- 修改构造函数以接受 `host_port` 参数
- 添加 `resolveAndUpdateAddress()` 方法用于 FQDN 重新解析
- 修改 `tryConnect()` 在失败时触发重新解析

**关键方法：**
```java
private void resolveAndUpdateAddress() {
    if (hostPort == null) return;
    try {
        rpc_address newAddr = resolveHostPort(hostPort);
        if (!newAddr.equals(address)) {
            logger.info("FQDN 解析为新 IP: {} -> {}", address, newAddr);
            manager_.updateReplicaSessionKey(this);
            this.address = newAddr;
        }
    } catch (Exception e) {
        logger.error("FQDN 解析失败: {}", hostPort, e);
    }
}
```

#### 2. ClusterManager

**用途：** 管理副本会话池，支持 FQDN

**更改：**
- 重载 `getReplicaSession(rpc_address, host_port)` 以接受两种地址类型
- 添加 `updateReplicaSessionKey(ReplicaSession)` 方法处理 FQDN 重新解析
- 保持 `ConcurrentHashMap<rpc_address, ReplicaSession>` 不变

**关键方法：**
```java
public ReplicaSession getReplicaSession(rpc_address address, host_port hostPort) {
    // 如果可用则使用 hostPort，否则仅使用 address
    // 如果缺少 hostPort，更新现有 session
}
```

#### 3. TableHandler

**用途：** 解析 meta 服务器配置并创建副本会话

**更改：**
- 修改 `initTableConfiguration()` 检查 `hp_primary` 和 `hp_secondaries` 字段
- 添加辅助方法 `resolveHostPortToRpcAddress(host_port)` 用于 FQDN 解析
- 当两者都可用时优先使用 `host_port` 而不是 `rpc_address`
- 将 `host_port` 传递给 `ClusterManager.getReplicaSession()`

**关键逻辑：**
```java
if (pConfig.isSetHp_primary() && pConfig.hp_primary.getHost() != null) {
    // 使用 host_port
    rConfig.primarySession = manager_.getReplicaSession(resolvedAddr, hpPrimary);
} else {
    // 使用 rpc_address (向后兼容)
    rConfig.primarySession = manager_.getReplicaSession(pConfig.primary, null);
}
```

## 实现细节

### 阶段 1：核心更改

1. **ReplicaSession 修改**
   - 添加 `host_port` 字段和构造函数重载
   - 实现 `resolveAndUpdateAddress()` 方法
   - 修改 `tryConnect()` 在失败时检查并重新解析 FQDN
   - 添加 FQDN 解析事件的日志记录

2. **ClusterManager 修改**
   - 添加 `getReplicaSession(rpc_address, host_port)` 重载
   - 实现 `updateReplicaSessionKey(ReplicaSession)` 方法
   - 更新会话创建以传递 `host_port` 参数

3. **TableHandler 修改**
   - 更新 `initTableConfiguration()` 解析 `host_port` 字段
   - 添加 `resolveHostPortToRpcAddress()` 辅助方法
   - 将 `host_port` 传递给会话创建调用

### 阶段 2：测试

1. **单元测试**
   - 测试 `ReplicaSession` FQDN 重新解析逻辑
   - 测试 `ClusterManager` 会话键更新
   - 测试 `TableHandler` 使用 `rpc_address` 和 `host_port` 的配置解析

2. **集成测试**
   - 针对返回 `host_port` 字段的 meta 服务器进行测试
   - 针对没有 `host_port` 字段的 meta 服务器进行测试（向后兼容性）
   - 测试连接失败和 FQDN 重新解析场景

### 阶段 3：文档

1. 更新客户端配置文档
2. 在 README 中添加 FQDN 支持说明
3. 记录连接失败和重新解析的行为

## 向后兼容性

该设计保持完全的向后兼容性：

1. **Meta 服务器兼容性**
   - 检查 `hp_primary != null && hp_primary.getHost() != null`
   - 如果 `host_port` 不可用则回退到 `rpc_address`
   - 同时适用于新旧 meta 服务器

2. **API 兼容性**
   - 现有的 `getReplicaSession(rpc_address)` 方法保持不变
   - 新的重载是增量添加，不会破坏现有功能
   - 使用 IP 地址的客户端代码继续工作

## 错误处理

1. **FQDN 解析失败**
   - 记录包含 host_port 详细信息的错误
   - 保留现有的 `rpc_address` 作为后备
   - 如果解析失败则不创建新会话

2. **连接失败**
   - 在重新解析之前检查 `host_port` 是否可用
   - 限制重新解析尝试以避免无限循环
   - 如果重新解析失败则回退到原始行为

## 性能考虑

1. **DNS 解析**
   - 在会话创建时进行初始解析（已在 `rpc_address.fromString()` 中发生）
   - 仅在连接失败时重新解析（不在每次请求时）
   - 此阶段没有基于 TTL 的缓存（可以稍后添加）

2. **会话管理**
   - 对于没有 `host_port` 的会话没有开销（仅 IP 连接）
   - 最小的内存开销（每个会话一个额外的 `host_port` 对象）
   - 映射键更新很少见（仅当 FQDN 解析为不同 IP 时）

## 未来增强

1. **DNS 缓存**
   - 为 DNS 解析结果添加 LRU 缓存
   - 实现基于 TTL 的缓存过期
   - 添加缓存大小限制

2. **运行时缓存管理**
   - 添加清除 DNS 缓存的命令
   - 添加使特定 FQDN 条目失效的命令
   - 添加统计监控

3. **高级重新解析**
   - 重新解析尝试的指数退避
   - 可配置的重新解析限制
   - 用于主动 IP 更新的后台重新解析

## 参考

- C++ 实现：`src/rpc/rpc_host_port.h`、`src/rpc/dns_resolver.cpp`
- Go 实现：`go-client/idl/base/host_port.go`
- 当前的 Java `host_port` 类：`java-client/src/main/java/org/apache/pegasus/base/host_port.java`
- IDL 定义：`idl/dsn.layer2.thrift`
