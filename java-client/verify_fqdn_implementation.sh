#!/bin/bash

# FQDN Implementation Verification Script
# This script verifies the FQDN support implementation without running tests

echo "=== Java Client FQDN Implementation Verification ==="
echo ""

PASSED=0
FAILED=0

# Function to check if a file contains specific content
check_content() {
    local file=$1
    local pattern=$2
    local description=$3

    if grep -q "$pattern" "$file"; then
        echo "✅ PASSED: $description"
        ((PASSED++))
        return 0
    else
        echo "❌ FAILED: $description"
        ((FAILED++))
        return 1
    fi
}

echo "1. Checking ReplicaSession Implementation..."
check_content \
    "src/main/java/org/apache/pegasus/rpc/async/ReplicaSession.java" \
    "private host_port hostPort;" \
    "ReplicaSession has host_port field"

check_content \
    "src/main/java/org/apache/pegasus/rpc/async/ReplicaSession.java" \
    "host_port hostPort," \
    "ReplicaSession constructor accepts host_port"

check_content \
    "src/main/java/org/apache/pegasus/rpc/async/ReplicaSession.java" \
    "resolveAndUpdateAddress" \
    "ReplicaSession has FQDN re-resolution method"

check_content \
    "src/main/java/org/apache/pegasus/rpc/async/ReplicaSession.java" \
    "public host_port getHostPort()" \
    "ReplicaSession has getHostPort() method"

echo ""
echo "2. Checking ClusterManager Implementation..."
check_content \
    "src/main/java/org/apache/pegasus/rpc/async/ClusterManager.java" \
    "public ReplicaSession getReplicaSession.*host_port hostPort" \
    "ClusterManager has getReplicaSession overload with host_port"

check_content \
    "src/main/java/org/apache/pegasus/rpc/async/ClusterManager.java" \
    "updateReplicaSessionKey" \
    "ClusterManager has updateReplicaSessionKey method"

echo ""
echo "3. Checking TableHandler Implementation..."
check_content \
    "src/main/java/org/apache/pegasus/rpc/async/TableHandler.java" \
    "resolveHostPortToRpcAddress" \
    "TableHandler has FQDN resolution helper method"

check_content \
    "src/main/java/org/apache/pegasus/rpc/async/TableHandler.java" \
    "isSetHp_primary" \
    "TableHandler checks for hp_primary field"

check_content \
    "src/main/java/org/apache/pegasus/rpc/async/TableHandler.java" \
    "getHp_primary" \
    "TableHandler accesses hp_primary field"

check_content \
    "src/main/java/org/apache/pegasus/rpc/async/TableHandler.java" \
    "public ReplicaSession tryConnect.*host_port hostPort" \
    "TableHandler has tryConnect overload with host_port"

echo ""
echo "4. Checking Test Files..."
check_content \
    "src/test/java/org/apache/pegasus/rpc/async/ReplicaSessionFQDNTest.java" \
    "testReplicaSessionWithHostPort" \
    "ReplicaSessionFQDNTest exists"

check_content \
    "src/test/java/org/apache/pegasus/rpc/async/ClusterManagerFQDNTest.java" \
    "testGetReplicaSessionWithHostPort" \
    "ClusterManagerFQDNTest exists"

check_content \
    "src/test/java/org/apache/pegasus/rpc/async/TableHandlerFQDNTest.java" \
    "testResolveHostPortToRpcAddress" \
    "TableHandlerFQDNTest exists"

echo ""
echo "5. Checking Documentation..."
check_content \
    "FQDN_TEST_GUIDE.md" \
    "Maven" \
    "Test guide exists"

check_content \
    "../docs/superpowers/IMPLEMENTATION_SUMMARY.md" \
    "FQDN" \
    "Implementation summary exists"

echo ""
echo "=== Verification Summary ==="
echo "PASSED: $PASSED"
echo "FAILED: $FAILED"
echo ""

if [ $FAILED -eq 0 ]; then
    echo "✅ All verification checks PASSED"
    echo ""
    echo "Implementation is complete and ready for testing!"
    echo ""
    echo "Next steps:"
    echo "1. Install Maven: brew install maven"
    echo "2. Run tests: mvn test -Dtest=*FQDNTest"
    echo "3. Review test guide: cat FQDN_TEST_GUIDE.md"
    exit 0
else
    echo "❌ Some verification checks FAILED"
    echo ""
    echo "Please review the failed checks above."
    exit 1
fi
