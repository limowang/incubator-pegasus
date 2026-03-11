# Java Client FQDN 测试指南

## 环境准备

### 1. 安装 Maven

Java client 使用 Maven 进行构建和测试。

```bash
# macOS
brew install maven

# Linux
sudo apt-get install maven

# 验证安装
mvn -version
```

### 2. 重新生成 Thrift 代码（可选）

如果需要重新生成 Thrift Java 代码：

```bash
# 设置环境变量
export THIRDPARTY_ROOT=/path/to/thirdparty

# 编译 Thrift
cd /Users/moli/incubator-pegasus
python3 build_tools/compile_thrift.py java
```

## 运行测试

### 编译项目

```bash
cd java-client
mvn clean compile
```

### 运行所有 FQDN 测试

```bash
cd java-client
mvn test -Dtest=*FQDNTest
```

### 运行特定测试

```bash
# 测试 ReplicaSession
mvn test -Dtest=ReplicaSessionFQDNTest

# 测试 ClusterManager
mvn test -Dtest=ClusterManagerFQDNTest

# 测试 TableHandler
mvn test -Dtest=TableHandlerFQDNTest
```

## 预期测试结果

### ReplicaSessionFQDNTest

- ✅ `testReplicaSessionWithHostPort` - 应该通过
  - 创建带 host_port 的 ReplicaSession
  - 验证 hostPort 字段正确设置

- ✅ `testReplicaSessionWithoutHostPort` - 应该通过
  - 创建不带 host_port 的 ReplicaSession（向后兼容）
  - 验证 hostPort 字段为 null

### ClusterManagerFQDNTest

- ✅ `testGetReplicaSessionWithHostPort` - 应该通过
  - 使用 host_port 创建会话
  - 验证会话正确存储 hostPort

- ✅ `testGetReplicaSessionBackwardCompatible` - 应该通过
  - 仅使用 rpc_address 创建会话
  - 验证向后兼容性

### TableHandlerFQDNTest

- ✅ `testResolveHostPortToRpcAddress` - 应该通过
  - 验证 host_port 解析为 rpc_address

- ⚠️ `testParseConfigurationWithHostPort` - 可能跳过
  - 需要实际运行的 meta 服务器

- ⚠️ `testParseConfigurationWithoutHostPort` - 可能跳过
  - 需要实际运行的 meta 服务器

## 集成测试

### 启动 Onebox 环境

```bash
cd /Users/moli/incubator-pegasus

# 启动 onebox（3 meta, 3 replica）
./run.sh start_onebox -m 3 -r 3

# 验证 onebox 运行
./run.sh list_onebox
```

### 配置 FQDN

编辑 onebox 配置文件，使用域名而不是 IP：

```bash
# 配置文件位置
vi run.sh
```

### 运行集成测试

```bash
cd java-client
mvn test -Dtest=FQDNIntegrationTest
```

## 故障排查

### 测试失败

1. **编译错误**
   ```bash
   # 清理并重新编译
   mvn clean compile
   ```

2. **依赖缺失**
   ```bash
   # 下载依赖
   mvn dependency:resolve
   ```

3. **Thrift 生成的代码缺失**
   ```bash
   # 重新生成 Thrift 代码
   # 参见上面的"重新生成 Thrift 代码"部分
   ```

### 常见问题

1. **Maven 版本不兼容**
   - 确保 Maven 版本 >= 3.6.0
   - 检查 Java 版本 >= 1.8

2. **测试依赖问题**
   ```bash
   # 跳过测试，只编译
   mvn clean compile -DskipTests
   ```

3. **Thrift 字段访问错误**
   - 确认 `partition_configuration` 类有 `isSetHp_primary()` 和 `getHp_primary()` 方法
   - 检查 `idl/dsn.layer2.thrift` 中字段定义

## 手动验证

如果无法运行测试，可以手动验证代码：

### 1. 检查编译错误

```bash
cd java-client
javac -d target/classes \
  src/main/java/org/apache/pegasus/base/host_port.java \
  src/main/java/org/apache/pegasus/base/rpc_address.java \
  src/main/java/org/apache/pegasus/rpc/async/ReplicaSession.java
```

### 2. 检查 API 兼容性

查看代码中关键的方法调用：
- `ReplicaSession` 构造函数是否接受 `host_port` 参数
- `ClusterManager.getReplicaSession()` 是否有重载版本
- `TableHandler` 是否调用 `isSetHp_primary()` 和 `getHp_primary()`

### 3. 日志验证

运行应用时，检查日志中是否有：
```
Resolved host_port localhost:34801 to 127.0.0.1:34801
Created new replica session for ... with host_port=...
```

## CI/CD 集成

在 CI 环境中运行测试：

```yaml
# .github/workflows/java-client-test.yml
name: Java Client Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Set up JDK
        uses: actions/setup-java@v4
        with:
          java-version: '8'
          distribution: 'adopt'
      - name: Install Maven
        run: sudo apt-get install -y maven
      - name: Run FQDN Tests
        run: |
          cd java-client
          mvn test -Dtest=*FQDNTest
```

## 性能测试

测试 FQDN 解析的性能影响：

```bash
# 使用 JMH 进行微基准测试
cd java-client
mvn exec:java -Dexec.mainClass="org.apache.pegasus.benchmark.FQDNSpeedBenchmark"
```

## 下一步

- ✅ 代码实现完成
- ⏳ 等待 Maven 环境
- ⏳ 运行单元测试
- ⏳ 运行集成测试
- ⏳ 性能测试
- ⏳ 文档更新

---

**最后更新：** 2026-03-11
**状态：** 代码已完成，等待测试环境
