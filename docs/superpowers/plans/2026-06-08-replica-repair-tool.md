# Pegasus Replica Repair Tool Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a Pegasus shell command that repairs corrupted replicas by rebuilding from SST files, with backup, verification, and detailed reporting.

**Architecture:** Shell command (`repair_replica`) that uses RocksDB SstFileReader to scan SST files, validates and recovers data, generates required metadata files (`.app-info`, `.init-info`), and includes safety mechanisms (backup, verification, rollback).

**Tech Stack:** C++, RocksDB (SstFileReader, SstFileWriter), Pegasus internal APIs (meta_store, replica_app_info, replica_init_info), JSON reporting

**Reference Implementation:** `src/shell/commands/local_partition_split.cpp` (use as template for SST file processing, metadata generation, and RocksDB operations)

---

## Chunk 1: Shell Command Registration and Basic Framework

### Task 1: Register repair_replica Command

**Files:**
- Modify: `src/shell/commands.h`
- Modify: `src/shell/main.cpp`

- [ ] **Step 1: Add command declaration to commands.h**

```cpp
// Add to src/shell/commands.h (after other command declarations)
extern const std::string repair_replica_help;
bool repair_replica(command_executor *e, shell_context *sc, arguments args);
```

- [ ] **Step 2: Register command in main.cpp**

```cpp
// Add to src/shell/main.cpp in the commands[] array (around line 61+)
{
    "repair_replica",
    "Repair corrupted Pegasus replicas by rebuilding from SST files",
    "<gpid> <replica_dir> <output_dir> [--backup_dir path] [--no_backup] "
    "[--report_file path] [--dry_run] [--skip_corrupted_records] "
    "[--max_corrupted_ratio ratio] [--verify_repair] [--help]",
    repair_replica,
},
```

- [ ] **Step 3: Verify compilation**

```bash
cd /home/wangguangshuo/apche/incubator-pegasus
./run.sh build -m pegasus_shell
```

Expected: Compilation succeeds (may have linker error for undefined `repair_replica` function - that's expected at this stage)

- [ ] **Step 4: Commit**

```bash
git add src/shell/commands.h src/shell/main.cpp
git commit -m "feat(shell): add repair_replica command registration

Add command declaration and registration for the new replica repair tool.
Command will repair corrupted Pegasus replicas by rebuilding from SST files.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

### Task 2: Create Basic Command File Structure

**Files:**
- Create: `src/shell/commands/replica_repair.cpp`

- [ ] **Step 1: Create basic file with includes and helper macros**

```cpp
// src/shell/commands/replica_repair.cpp
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

#include <fmt/core.h>
#include <rocksdb/db.h>
#include <rocksdb/env.h>
#include <rocksdb/options.h>
#include <stdio.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "base/meta_store.h"
#include "common/gpid.h"
#include "dsn.layer2_types.h"
#include "replica/replication_app_base.h"
#include "shell/command_executor.h"
#include "shell/commands.h"
#include "utils/error_code.h"
#include "utils/filesystem.h"
#include "utils/fmt_logging.h"

// Error handling macros (from local_partition_split)
#define RETURN_FALSE_IF_NOT(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            fmt::print(stderr, "Check failed: " fmt "\n", ##__VA_ARGS__); \
            return false; \
        } \
    } while (0)

#define RETURN_FALSE_IF_NON_OK(err, fmt, ...) \
    do { \
        if (err != dsn::ERR_OK) { \
            fmt::print(stderr, "Error: " fmt "\n", ##__VA_ARGS__); \
            return false; \
        } \
    } while (0)

#define RETURN_FALSE_IF_NON_RDB_OK(status, fmt, ...) \
    do { \
        if (!(status).ok()) { \
            fmt::print(stderr, "RocksDB error: " fmt "\n", ##__VA_ARGS__); \
            return false; \
        } \
    } while (0)

const std::string repair_replica_help =
    "<gpid> <replica_dir> <output_dir> [--backup_dir path] [--no_backup] "
    "[--report_file path] [--dry_run] [--skip_corrupted_records] "
    "[--max_corrupted_ratio ratio] [--verify_repair] [--help]";

// Basic data structures
struct RepairConfig {
    int32_t app_id = 0;
    int32_t partition_id = 0;
    std::string replica_dir;
    std::string output_dir;
    std::string backup_dir;
    std::string report_file;
    bool dry_run = false;
    bool skip_corrupted = false;
    bool create_backup = true;
    bool verify_repair = true;
    double max_corrupted_ratio = 0.5;
};

struct RepairStats {
    int64_t total_sst_files = 0;
    int64_t corrupted_sst_files = 0;
    int64_t total_records = 0;
    int64_t recovered_records = 0;
    int64_t skipped_records = 0;
    int64_t data_size_bytes = 0;
    double duration_seconds = 0;
};

struct RepairResult {
    bool success = false;
    std::string error_message;
    RepairStats stats;
    std::vector<std::string> warnings;
    std::string backup_path;
    bool verification_passed = false;
};

// Main command function
bool repair_replica(command_executor *e, shell_context *sc, arguments args) {
    fmt::print(stdout, "Replica repair tool - not yet implemented\n");
    fmt::print(stdout, "GPID: {}, Replica: {}, Output: {}\n",
               args.argv[1], args.argv[2], args.argv[3]);
    return true;
}
```

- [ ] **Step 2: Verify compilation**

```bash
./run.sh build -m pegasus_shell
```

Expected: Compilation succeeds with basic stub implementation

- [ ] **Step 3: Test basic command invocation**

```bash
./run.sh shell
> help
```

Expected: `repair_replica` should appear in command list

- [ ] **Step 4: Commit**

```bash
git add src/shell/commands/replica_repair.cpp
git commit -m "feat(replica_repair): add basic command structure

Add basic file structure with includes, macros, and stub implementation.
Tool will be implemented incrementally.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Chunk 2: Parameter Parsing and Validation

### Task 3: Parse Command-line Arguments

**Files:**
- Modify: `src/shell/commands/replica_repair.cpp`

- [ ] **Step 1: Add argument parsing logic**

```cpp
// Add before the repair_replica function:
bool parse_arguments(arguments args, RepairConfig& config, std::string& error_msg) {
    // Need at least: gpid replica_dir output_dir
    if (args.argc < 4) {
        error_msg = fmt::format("Insufficient arguments. Usage: {}\n", repair_replica_help);
        return false;
    }

    // Parse gpid (format: app_id.partition_id)
    std::string gpid_str = args.argv[1];
    if (sscanf(gpid_str.c_str(), "%d.%d", &config.app_id, &config.partition_id) != 2) {
        error_msg = fmt::format("Invalid GPID format: {}. Expected: app_id.partition_id\n", gpid_str);
        return false;
    }

    config.replica_dir = args.argv[2];
    config.output_dir = args.argv[3];

    // Parse optional arguments
    for (int i = 4; i < args.argc; i++) {
        std::string arg = args.argv[i];

        if (arg == "--help" || arg == "-h") {
            fmt::print(stdout, "Usage: {}\n", repair_replica_help);
            fmt::print(stdout, "\nOptions:\n");
            fmt::print(stdout, "  --backup_dir <path>     Backup directory (auto-generated if not specified)\n");
            fmt::print(stdout, "  --no_backup              Skip backup creation (NOT recommended)\n");
            fmt::print(stdout, "  --report_file <path>     JSON report file path\n");
            fmt::print(stdout, "  --dry_run                Diagnose only without actual repair\n");
            fmt::print(stdout, "  --skip_corrupted_records Skip corrupted records and continue\n");
            fmt::print(stdout, "  --max_corrupted_ratio    Maximum corrupted file ratio (0.0-1.0, default: 0.5)\n");
            fmt::print(stdout, "  --verify_repair           Verify repaired replica (default: true)\n");
            return false;
        }

        if (arg == "--no_backup") {
            config.create_backup = false;
        } else if (arg == "--dry_run") {
            config.dry_run = true;
        } else if (arg == "--skip_corrupted_records") {
            config.skip_corrupted = true;
        } else if (arg == "--verify_repair") {
            config.verify_repair = true;
        } else if (i + 1 < args.argc) {
            // Arguments that take a value
            if (arg == "--backup_dir") {
                config.backup_dir = args.argv[++i];
            } else if (arg == "--report_file") {
                config.report_file = args.argv[++i];
            } else if (arg == "--max_corrupted_ratio") {
                try {
                    config.max_corrupted_ratio = std::stod(args.argv[++i]);
                    if (config.max_corrupted_ratio < 0.0 || config.max_corrupted_ratio > 1.0) {
                        error_msg = "max_corrupted_ratio must be between 0.0 and 1.0";
                        return false;
                    }
                } catch (...) {
                    error_msg = "Invalid max_corrupted_ratio value";
                    return false;
                }
            }
        } else {
            error_msg = fmt::format("Unknown argument: {}", arg);
            return false;
        }
    }

    // Auto-generate backup dir if not specified and backup is enabled
    if (config.create_backup && config.backup_dir.empty()) {
        config.backup_dir = fmt::format("/tmp/replica_backup_{}.{}.{}",
                                        config.app_id, config.partition_id,
                                        std::chrono::system_clock::now().time_since_epoch().count());
    }

    return true;
}
```

- [ ] **Step 2: Update repair_replica to use parsing**

```cpp
// Replace the stub repair_replica function with:
bool repair_replica(command_executor *e, shell_context *sc, arguments args) {
    RepairConfig config;
    std::string error_msg;

    if (!parse_arguments(args, config, error_msg)) {
        if (error_msg.empty() || error_msg.find("Usage:") != std::string::npos) {
            // Help was requested or parsing info
            return error_msg.empty();
        }
        fmt::print(stderr, "Error: {}\n", error_msg);
        return false;
    }

    // Display parsed configuration
    fmt::print(stdout, "Replica Repair Configuration:\n");
    fmt::print(stdout, "  GPID: {}.{}\n", config.app_id, config.partition_id);
    fmt::print(stdout, "  Replica Dir: {}\n", config.replica_dir);
    fmt::print(stdout, "  Output Dir: {}\n", config.output_dir);
    fmt::print(stdout, "  Backup Dir: {}\n", config.backup_dir.empty() ? "(none, backup disabled)" : config.backup_dir);
    fmt::print(stdout, "  Dry Run: {}\n", config.dry_run ? "yes" : "no");
    fmt::print(stdout, "  Skip Corrupted: {}\n", config.skip_corrupted ? "yes" : "no");
    fmt::print(stdout, "  Max Corrupted Ratio: {}\n", config.max_corrupted_ratio);

    return true;
}
```

- [ ] **Step 3: Verify compilation**

```bash
./run.sh build -m pegasus_shell
```

Expected: Compilation succeeds

- [ ] **Step 4: Test argument parsing**

```bash
./run.sh shell
> repair_replica --help
> repair_replica 1.1 /path/to/replica /path/to/output
```

Expected: Help message displays correctly, configuration prints with parsed values

- [ ] **Step 5: Commit**

```bash
git add src/shell/commands/replica_repair.cpp
git commit -m "feat(replica_repair): add argument parsing and validation

Parse command-line arguments including gpid, directories, and options.
Display configuration summary after parsing.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

### Task 4: Add Directory Validation

**Files:**
- Modify: `src/shell/commands/replica_repair.cpp`

- [ ] **Step 1: Add validation function**

```cpp
// Add after parse_arguments:
bool validate_directories(const RepairConfig& config, std::string& error_msg) {
    // Check if replica directory exists
    if (!dsn::utils::filesystem::directory_exists(config.replica_dir)) {
        error_msg = fmt::format("Replica directory does not exist: {}", config.replica_dir);
        return false;
    }

    // Check if replica directory has the expected structure
    auto rdb_dir = dsn::utils::filesystem::path_combine(
        config.replica_dir,
        dsn::utils::filesystem::path_combine("rdb", "data")
    );

    if (!dsn::utils::filesystem::directory_exists(rdb_dir)) {
        error_msg = fmt::format("Replica directory does not contain rdb/data subdirectory: {}",
                               config.replica_dir);
        return false;
    }

    // Check if output directory's parent exists (we'll create the output dir itself)
    std::string output_parent = dsn::utils::filesystem::parent_directory(config.output_dir);
    if (!dsn::utils::filesystem::directory_exists(output_parent)) {
        error_msg = fmt::format("Output directory parent does not exist: {}", output_parent);
        return false;
    }

    // Check if backup directory's parent exists (if backup is enabled)
    if (config.create_backup && !config.backup_dir.empty()) {
        std::string backup_parent = dsn::utils::filesystem::parent_directory(config.backup_dir);
        if (!dsn::utils::filesystem::directory_exists(backup_parent)) {
            error_msg = fmt::format("Backup directory parent does not exist: {}", backup_parent);
            return false;
        }
    }

    return true;
}
```

- [ ] **Step 2: Update repair_replica to validate**

```cpp
// Add after parse_arguments call:
    // Validate directories
    if (!validate_directories(config, error_msg)) {
        fmt::print(stderr, "Error: {}\n", error_msg);
        return false;
    }

    fmt::print(stdout, "Directory validation passed\n");
```

- [ ] **Step 3: Verify compilation**

```bash
./run.sh build -m pegasus_shell
```

- [ ] **Step 4: Test validation**

```bash
./run.sh shell
> repair_replica 1.1 /nonexistent/path /tmp/output
```

Expected: Error message about non-existent replica directory

- [ ] **Step 5: Commit**

```bash
git add src/shell/commands/replica_repair.cpp
git commit -m "feat(replica_repair): add directory validation

Validate that replica directory exists and has correct structure.
Check parent directories for output and backup locations.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Chunk 3: Backup and Rollback Mechanism

### Task 5: Implement Backup Creation

**Files:**
- Modify: `src/shell/commands/replica_repair.cpp`

- [ ] **Step 1: Add backup function**

```cpp
// Add after validate_directories:
bool create_backup(const std::string& replica_dir, const std::string& backup_dir, std::string& error_msg) {
    fmt::print(stdout, "Creating backup: {} -> {}\n", replica_dir, backup_dir);

    // Check if backup directory already exists
    if (dsn::utils::filesystem::directory_exists(backup_dir)) {
        error_msg = fmt::format("Backup directory already exists: {}", backup_dir);
        return false;
    }

    // Create backup directory
    if (!dsn::utils::filesystem::create_directory(backup_dir)) {
        error_msg = fmt::format("Failed to create backup directory: {}", backup_dir);
        return false;
    }

    // Use system copy command (recursive, preserving attributes)
    std::string cmd = fmt::format("cp -r \"{}\" \"{}\"", replica_dir, backup_dir);
    int ret = system(cmd.c_str());

    if (ret != 0) {
        error_msg = fmt::format("Backup command failed with code: {}", ret);
        // Cleanup partial backup
        dsn::utils::filesystem::remove_path(backup_dir);
        return false;
    }

    fmt::print(stdout, "Backup created successfully\n");
    return true;
}

bool verify_backup(const std::string& backup_dir, std::string& error_msg) {
    // Check if backup has expected structure
    auto rdb_dir = dsn::utils::filesystem::path_combine(
        backup_dir,
        dsn::utils::filesystem::path_combine("rdb", "data")
    );

    if (!dsn::utils::filesystem::directory_exists(rdb_dir)) {
        error_msg = fmt::format("Backup verification failed: rdb/data directory missing in {}", backup_dir);
        return false;
    }

    fmt::print(stdout, "Backup verified successfully\n");
    return true;
}
```

- [ ] **Step 2: Add backup call to repair_replica**

```cpp
// Add after directory validation, before "Directory validation passed":
    // Step 0: Create backup
    if (config.create_backup) {
        if (!create_backup(config.replica_dir, config.backup_dir, error_msg)) {
            fmt::print(stderr, "Error: {}\n", error_msg);
            return false;
        }

        if (!verify_backup(config.backup_dir, error_msg)) {
            fmt::print(stderr, "Error: {}\n", error_msg);
            // Cleanup failed backup
            dsn::utils::filesystem::remove_path(config.backup_dir);
            return false;
        }
    } else {
        fmt::print(stdout, "WARNING: Backup creation is disabled (NOT recommended)\n");
    }
```

- [ ] **Step 3: Verify compilation**

```bash
./run.sh build -m pegasus_shell
```

- [ ] **Step 4: Test backup creation**

Create a test replica directory first, then:
```bash
./run.sh shell
> repair_replica 1.1 /path/to/test/replica /tmp/output --backup_dir /tmp/test_backup
```

Expected: Backup created and verified successfully

- [ ] **Step 5: Commit**

```bash
git add src/shell/commands/replica_repair.cpp
git commit -m "feat(replica_repair): add backup creation and verification

Implement backup mechanism that copies entire replica directory.
Verify backup integrity before proceeding.
Support --no_backup option to skip (not recommended).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

### Task 6: Implement Rollback Function

**Files:**
- Modify: `src/shell/commands/replica_repair.cpp`

- [ ] **Step 1: Add rollback function**

```cpp
// Add after verify_backup:
bool rollback_to_backup(const std::string& backup_dir, const std::string& output_dir, std::string& error_msg) {
    fmt::print(stdout, "Rolling back to backup...\n");

    // Remove failed repair attempt
    if (dsn::utils::filesystem::directory_exists(output_dir)) {
        if (!dsn::utils::filesystem::remove_path(output_dir)) {
            error_msg = fmt::format("Failed to remove failed repair directory: {}", output_dir);
            return false;
        }
    }

    fmt::print(stdout, "Rollback completed\n");
    return true;
}
```

- [ ] **Step 2: Add cleanup function**

```cpp
// Add after rollback_to_backup:
bool cleanup_backup(const std::string& backup_dir) {
    if (backup_dir.empty() || !dsn::utils::filesystem::directory_exists(backup_dir)) {
        return true; // Nothing to cleanup
    }

    fmt::print(stdout, "Cleaning up backup: {}\n", backup_dir);
    return dsn::utils::filesystem::remove_path(backup_dir);
}
```

- [ ] **Step 3: Verify compilation**

```bash
./run.sh build -m pegasus_shell
```

- [ ] **Step 4: Commit**

```bash
git add src/shell/commands/replica_repair.cpp
git commit -m "feat(replica_repair): add rollback and cleanup functions

Implement rollback mechanism to restore from backup on failure.
Add cleanup function to remove backup after successful repair.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Chunk 4: Metadata Loading

### Task 7: Implement Metadata Loading Functions

**Files:**
- Modify: `src/shell/commands/replica_repair.cpp`

- [ ] **Step 1: Add metadata loading functions**

```cpp
// Add after cleanup_backup, before repair_replica:
dsn::error_code load_metadata_from_replica(const std::string& replica_dir,
                                         dsn::app_info& app_info,
                                         dsn::replication::replica_init_info& init_info,
                                         std::string& error_msg) {
    // Try to load .app-info
    auto app_info_path = dsn::utils::filesystem::path_combine(
        replica_dir, dsn::replication::replica_app_info::kAppInfo);

    auto err = dsn::utils::load_rjobj_from_file(app_info_path, &app_info);
    if (err == dsn::ERR_OK) {
        fmt::print(stdout, "Loaded app_info from {}\n", app_info_path);
    } else {
        fmt::print(stdout, "WARNING: Could not load app_info from {} (error: {}). Will use defaults.\n",
                   app_info_path, err);
    }

    // Try to load .init-info
    auto init_info_path = dsn::utils::filesystem::path_combine(
        replica_dir, dsn::replication::replica_init_info::kInitInfo);

    err = dsn::utils::load_rjobj_from_file(init_info_path, &init_info);
    if (err == dsn::ERR_OK) {
        fmt::print(stdout, "Loaded init_info from {}\n", init_info_path);
    } else {
        fmt::print(stdout, "WARNING: Could not load init_info from {} (error: {}). Will use defaults.\n",
                   init_info_path, err);
    }

    return dsn::ERR_OK;
}

dsn::error_code generate_default_metadata(const RepairConfig& config,
                                         dsn::app_info& app_info,
                                         dsn::replication::replica_init_info& init_info) {
    // Set default app_info
    app_info.app_id = config.app_id;
    app_info.app_name = fmt::format("app_{}", config.app_id);
    app_info.app_type = "pegasus";
    app_info.partition_count = 1; // Will be updated from actual data if available
    app_info.status = dsn::app_status::AS_AVAILABLE;

    // Set default init_info
    init_info.init_ballot = 0;
    init_info.init_durable_decree = 0;
    init_info.init_offset_in_shared_log = 0;
    init_info.init_offset_in_private_log = 0;

    return dsn::ERR_OK;
}
```

- [ ] **Step 2: Add metadata loading to repair_replica**

```cpp
// Add after backup creation, in the main flow:
    // Step 3: Load metadata
    dsn::app_info app_info;
    dsn::replication::replica_init_info init_info;

    load_metadata_from_replica(config.replica_dir, app_info, init_info, error_msg);

    // Ensure we have at least default values
    generate_default_metadata(config, app_info, init_info);

    fmt::print(stdout, "App ID: {}, App Name: {}, Partition Count: {}\n",
               app_info.app_id, app_info.app_name, app_info.partition_count);
```

- [ ] **Step 3: Verify compilation**

```bash
./run.sh build -m pegasus_shell
```

- [ ] **Step 4: Test metadata loading**

```bash
./run.sh shell
> repair_replica 1.1 /path/to/test/replica /tmp/output
```

Expected: Metadata loaded (or warnings about missing files), default values used

- [ ] **Step 5: Commit**

```bash
git add src/shell/commands/replica_repair.cpp
git commit -m "feat(replica_repair): add metadata loading

Load .app-info and .init-info from original replica.
Fall back to default values if files are missing or corrupted.
Display loaded metadata information.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Chunk 5: SST File Scanning

### Task 8: Implement SST File Discovery

**Files:**
- Modify: `src/shell/commands/replica_repair.cpp`

- [ ] **Step 1: Add SST file discovery function**

```cpp
// Add after generate_default_metadata, using local_partition_split pattern:
bool discover_sst_files(const std::string& replica_dir,
                       std::vector<std::string>& sst_files,
                       std::string& error_msg) {
    auto rdb_dir = dsn::utils::filesystem::path_combine(
        replica_dir,
        dsn::utils::filesystem::path_combine("rdb", "data")
    );

    // Get all subdirectories (should be numbered like 000001, 000002, etc.)
    std::vector<std::string> subdirs;
    if (!dsn::utils::filesystem::get_subdirectories(rdb_dir, subdirs, false)) {
        error_msg = fmt::format("Failed to list subdirectories in {}", rdb_dir);
        return false;
    }

    // Collect all .sst files
    for (const auto& subdir : subdirs) {
        std::vector<std::string> files;
        if (!dsn::utils::filesystem::get_subfiles(subdir, files, false)) {
            continue;
        }

        for (const auto& file : files) {
            if (file.size() >= 4 && file.substr(file.size() - 4) == ".sst") {
                sst_files.push_back(file);
            }
        }
    }

    fmt::print(stdout, "Found {} SST files\n", sst_files.size());
    return true;
}
```

- [ ] **Step 2: Add to repair_replica main flow**

```cpp
// Add after metadata loading:
    // Step 4: Discover SST files
    std::vector<std::string> sst_files;
    if (!discover_sst_files(config.replica_dir, sst_files, error_msg)) {
        fmt::print(stderr, "Error: {}\n", error_msg);
        if (config.create_backup) {
            cleanup_backup(config.backup_dir);
        }
        return false;
    }

    if (sst_files.empty()) {
        fmt::print(stderr, "Error: No SST files found in replica directory\n");
        if (config.create_backup) {
            cleanup_backup(config.backup_dir);
        }
        return false;
    }

    fmt::print(stdout, "SST file discovery completed\n");
```

- [ ] **Step 3: Verify compilation**

```bash
./run.sh build -m pegasus_shell
```

- [ ] **Step 4: Test SST file discovery**

```bash
./run.sh shell
> repair_replica 1.1 /path/to/test/replica /tmp/output
```

Expected: Shows number of SST files found

- [ ] **Step 5: Commit**

```bash
git add src/shell/commands/replica_repair.cpp
git commit -m "feat(replica_repair): add SST file discovery

Scan replica directory and collect all SST files.
Check for empty replica case.
Follow local_partition_split pattern for directory structure.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Chunk 6: RocksDB Operations - Opening Original Database

### Task 9: Implement RocksDB Opening Functions

**Files:**
- Modify: `src/shell/commands/replica_repair.cpp`

- [ ] **Step 1: Add RocksDB helper functions (from local_partition_split)**

```cpp
// Add after discover_sst_files, using local_partition_split code:
bool open_rocksdb(const rocksdb::DBOptions& db_opts,
                 const std::string& rdb_dir,
                 bool read_only,
                 const std::vector<rocksdb::ColumnFamilyDescriptor>& cf_dscs,
                 std::vector<rocksdb::ColumnFamilyHandle*>* cf_hdls,
                 rocksdb::DB** db) {
    CHECK_NOTNULL(cf_hdls, "");
    CHECK_NOTNULL(db, "");

    if (read_only) {
        RETURN_FALSE_IF_NON_RDB_OK(
            rocksdb::DB::OpenForReadOnly(db_opts, rdb_dir, cf_dscs, cf_hdls, db),
            "open rocksdb in read-only mode failed for '{}'",
            rdb_dir);
    } else {
        RETURN_FALSE_IF_NON_RDB_OK(rocksdb::DB::Open(db_opts, rdb_dir, cf_dscs, cf_hdls, db),
                                   "open rocksdb failed for '{}'",
                                   rdb_dir);
    }

    CHECK_EQ(2, cf_hdls->size());
    CHECK_EQ(pegasus::server::meta_store::DATA_COLUMN_FAMILY_NAME, (*cf_hdls)[0]->GetName());
    CHECK_EQ(pegasus::server::meta_store::META_COLUMN_FAMILY_NAME, (*cf_hdls)[1]->GetName());

    return true;
}

void release_db(std::vector<rocksdb::ColumnFamilyHandle*>* cf_hdls, rocksdb::DB** db) {
    CHECK_NOTNULL(cf_hdls, "");
    CHECK_NOTNULL(db, "");
    for (auto cf_hdl : *cf_hdls) {
        delete cf_hdl;
    }
    cf_hdls->clear();
    delete *db;
    *db = nullptr;
}
```

- [ ] **Step 2: Add function to read metadata from RocksDB**

```cpp
// Add after release_db:
bool read_rocksdb_metadata(const std::string& rdb_dir,
                          uint64_t& last_committed_decree,
                          uint32_t& pegasus_data_version,
                          std::string& error_msg) {
    rocksdb::DBOptions db_opts;
    const std::vector<rocksdb::ColumnFamilyDescriptor> cf_dscs(
        {{pegasus::server::meta_store::DATA_COLUMN_FAMILY_NAME, {}},
         {pegasus::server::meta_store::META_COLUMN_FAMILY_NAME, {}}});

    std::vector<rocksdb::ColumnFamilyHandle*> cf_hdls;
    rocksdb::DB* db = nullptr;

    if (!open_rocksdb(db_opts, rdb_dir, true, cf_dscs, &cf_hdls, &db)) {
        error_msg = "Failed to open RocksDB in read-only mode";
        return false;
    }

    // Use meta_store to read metadata
    auto ms = std::make_unique<pegasus::server::meta_store>(rdb_dir.c_str(), db, cf_hdls[1]);

    bool ret = true;
    if (ms->get_last_flushed_decree(&last_committed_decree) != dsn::ERR_OK) {
        error_msg = "Failed to get last flushed decree";
        ret = false;
    }

    if (ret && ms->get_data_version(&pegasus_data_version) != dsn::ERR_OK) {
        error_msg = "Failed to get data version";
        ret = false;
    }

    release_db(&cf_hdls, &db);
    return ret;
}
```

- [ ] **Step 3: Add RocksDB opening attempt to repair_replica**

```cpp
// Add after SST file discovery:
    // Step 5: Try to read metadata from RocksDB (if possible)
    auto rdb_dir = dsn::utils::filesystem::path_combine(
        config.replica_dir,
        dsn::utils::filesystem::path_combine("rdb", "data")
    );

    uint64_t last_committed_decree = 0;
    uint32_t pegasus_data_version = 0;

    bool rocksdb_readable = read_rocksdb_metadata(rdb_dir, last_committed_decree,
                                                  pegasus_data_version, error_msg);
    if (rocksdb_readable) {
        fmt::print(stdout, "RocksDB metadata: last_decree={}, data_version={}\n",
                   last_committed_decree, pegasus_data_version);
        // Update init_info with actual decree
        init_info.init_durable_decree = last_committed_decree;
    } else {
        fmt::print(stdout, "INFO: Could not read RocksDB metadata (expected if corrupted): {}\n",
                   error_msg);
    }
```

- [ ] **Step 4: Verify compilation**

```bash
./run.sh build -m pegasus_shell
```

- [ ] **Step 5: Test RocksDB opening**

```bash
./run.sh shell
> repair_replica 1.1 /path/to/test/replica /tmp/output
```

Expected: Attempts to read RocksDB metadata, displays result

- [ ] **Step 6: Commit**

```bash
git add src/shell/commands/replica_repair.cpp
git commit -m "feat(replica_repair): add RocksDB metadata reading

Implement functions to open RocksDB in read-only mode and read metadata.
Use meta_store to get last flushed decree and data version.
Handle gracefully when RocksDB is corrupted (primary use case).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Plan Continues in Next File

Due to length constraints, this plan will be completed in a separate document. The remaining chunks will cover:

**Chunk 7**: SST File Repair Implementation
- SstFileReader-based file processing
- Data validation and record copying
- Error handling and corrupted record skipping

**Chunk 8**: New Database Creation and SST Import
- Create new RocksDB instance
- Import repaired SST files
- Set metadata in new database

**Chunk 9**: Metadata File Generation
- Generate .app-info file
- Generate .init-info file
- Use local_partition_split patterns

**Chunk 10**: Verification Module
- Verify repaired database can open
- Check metadata consistency
- Sample data verification

**Chunk 11**: Progress Reporting and JSON Output
- Real-time progress tracking
- JSON report generation
- Final statistics and cleanup

**Chunk 12**: Testing and Integration
- Unit tests
- Integration tests with onebox
- Performance validation

---
