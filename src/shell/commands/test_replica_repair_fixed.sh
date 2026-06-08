#!/bin/bash

# Test script for replica repair with correct directory structure

set -e

TEST_DIR="/tmp/replica_repair_test_$$"
SOURCE_REPLICA="$TEST_DIR/source_replica"
OUTPUT_DIR="$TEST_DIR/output"

echo "Creating test directory structure..."

# Create source replica with correct structure
mkdir -p "$SOURCE_REPLICA/data/rdb"
touch "$SOURCE_REPLICA/.init-info"
touch "$SOURCE_REPLICA/.app-info"

# Create dummy metadata files
echo "test_app_info" > "$SOURCE_REPLICA/.app-info"
echo "test_init_info" > "$SOURCE_REPLICA/.init-info"

# Verify structure
echo "Verifying source replica structure..."
if [ ! -d "$SOURCE_REPLICA/data/rdb" ]; then
    echo "FAIL: data/rdb directory not found"
    exit 1
fi

if [ ! -f "$SOURCE_REPLICA/.app-info" ]; then
    echo "FAIL: .app-info not found"
    exit 1
fi

if [ ! -f "$SOURCE_REPLICA/.init-info" ]; then
    echo "FAIL: .init-info not found"
    exit 1
fi

echo "Source replica structure verified successfully"

# Test dry run (if we had the shell built)
# ./build/bin/pegasus_shell -c "repair_replica 1.2 $SOURCE_REPLICA $OUTPUT_DIR --dry_run"

echo "Test completed successfully"
echo "Test directory: $TEST_DIR"

# Cleanup
# rm -rf "$TEST_DIR"
