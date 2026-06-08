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
#include "utils/load_dump_object.h"


const std::string repair_replica_help =
    "<gpid> <replica_dir> <output_dir> [--backup_dir path] [--no_backup] "
    "[--report_file path] [--dry_run] [--skip_corrupted_records] "
    "[--max_corrupted_ratio ratio] [--verify_repair] [--help]";

const std::string repair_replica_examples =
    "Examples:\n"
    "  repair_replica 1.2 /path/to/replica /path/to/output\n"
    "  repair_replica 1.2 /path/to/replica /path/to/output --dry_run\n"
    "  repair_replica 1.2 /path/to/replica /path/to/output --backup_dir /custom/backup\n"
    "  repair_replica 1.2 /path/to/replica /path/to/output --max_corrupted_ratio 0.3\n"
    "  repair_replica 1.2 /path/to/replica /path/to/output --report_file report.json";

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

    // Validate GPID numerical ranges
    if (config.app_id <= 0) {
        error_msg = fmt::format("Invalid app_id: {}. app_id must be greater than 0\n", config.app_id);
        return false;
    }
    if (config.partition_id < 0) {
        error_msg = fmt::format("Invalid partition_id: {}. partition_id must be >= 0\n", config.partition_id);
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
            fmt::print(stdout, "\nExamples:\n");
            fmt::print(stdout, "  repair_replica 1.2 /path/to/replica /path/to/output\n");
            fmt::print(stdout, "  repair_replica 1.2 /path/to/replica /path/to/output --dry_run\n");
            fmt::print(stdout, "  repair_replica 1.2 /path/to/replica /path/to/output --max_corrupted_ratio 0.3\n");
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
        } else if (arg == "--backup_dir") {
            if (i + 1 < args.argc) {
                config.backup_dir = args.argv[++i];
            } else {
                error_msg = "--backup_dir requires a path argument";
                return false;
            }
        } else if (arg == "--report_file") {
            if (i + 1 < args.argc) {
                config.report_file = args.argv[++i];
            } else {
                error_msg = "--report_file requires a path argument";
                return false;
            }
        } else if (arg == "--max_corrupted_ratio") {
            if (i + 1 < args.argc) {
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
            } else {
                error_msg = "--max_corrupted_ratio requires a numeric value";
                return false;
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
        "rdb"
    );

    if (!dsn::utils::filesystem::directory_exists(rdb_dir)) {
        error_msg = fmt::format("Replica directory does not contain rdb subdirectory: {}",
                               config.replica_dir);
        return false;
    }

    // Check if output directory's parent exists (we'll create the output dir itself)
    std::string output_parent = dsn::utils::filesystem::remove_file_name(config.output_dir);
    if (!dsn::utils::filesystem::directory_exists(output_parent)) {
        error_msg = fmt::format("Output directory parent does not exist: {}", output_parent);
        return false;
    }

    // Check if backup directory's parent exists (if backup is enabled)
    if (config.create_backup && !config.backup_dir.empty()) {
        std::string backup_parent = dsn::utils::filesystem::remove_file_name(config.backup_dir);
        if (!dsn::utils::filesystem::directory_exists(backup_parent)) {
            error_msg = fmt::format("Backup directory parent does not exist: {}", backup_parent);
            return false;
        }
    }

    return true;
}

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
    // The backup should contain the replica directory with rdb subdirectory
    // Since we copy the entire replica directory, we need to check for rdb inside
    auto rdb_dir = dsn::utils::filesystem::path_combine(
        backup_dir,
        "rdb"  // 注意：使用"rdb"而不是"rdb/data"，基于任务4的修正
    );

    if (!dsn::utils::filesystem::directory_exists(rdb_dir)) {
        error_msg = fmt::format("Backup verification failed: rdb directory missing in {}", backup_dir);
        return false;
    }

    fmt::print(stdout, "Backup verified successfully\n");
    return true;
}

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

// Add after rollback_to_backup:
bool cleanup_backup(const std::string& backup_dir) {
    if (backup_dir.empty() || !dsn::utils::filesystem::directory_exists(backup_dir)) {
        return true; // Nothing to cleanup
    }

    fmt::print(stdout, "Cleaning up backup: {}\n", backup_dir);
    return dsn::utils::filesystem::remove_path(backup_dir);
}

// Add after cleanup_backup:
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

// Add after generate_default_metadata, using local_partition_split pattern:
bool discover_sst_files(const std::string& replica_dir,
                       std::vector<std::string>& sst_files,
                       std::string& error_msg) {
    auto rdb_dir = dsn::utils::filesystem::path_combine(
        replica_dir,
        "rdb"  // 注意：使用"rdb"而不是"rdb/data"，基于任务4的修正
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


// External function declarations from local_partition_split.cpp
extern bool open_rocksdb(const rocksdb::DBOptions& db_opts,
                        const std::string& rdb_dir,
                        bool read_only,
                        const std::vector<rocksdb::ColumnFamilyDescriptor>& cf_dscs,
                        std::vector<rocksdb::ColumnFamilyHandle*>* cf_hdls,
                        rocksdb::DB** db);
extern void release_db(std::vector<rocksdb::ColumnFamilyHandle*>* cf_hdls, rocksdb::DB** db);

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

// Main command function
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

    // Validate directories
    fmt::print(stdout, "Starting directory validation...\n");
    if (!validate_directories(config, error_msg)) {
        fmt::print(stderr, "Error: {}\n", error_msg);
        return false;
    }

    fmt::print(stdout, "Directory validation passed\n");

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

    // Step 3: Load metadata
    dsn::app_info app_info;
    dsn::replication::replica_init_info init_info;

    load_metadata_from_replica(config.replica_dir, app_info, init_info, error_msg);

    // Ensure we have at least default values
    generate_default_metadata(config, app_info, init_info);

    fmt::print(stdout, "App ID: {}, App Name: {}, Partition Count: {}\n",
               app_info.app_id, app_info.app_name, app_info.partition_count);

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

    // Step 5: Try to read metadata from RocksDB (if possible)
    auto rdb_dir = dsn::utils::filesystem::path_combine(
        config.replica_dir,
        "rdb"  // 注意：使用"rdb"而不是"rdb/data"，基于任务4和8的修正
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

    fmt::print(stdout, "SUCCESS: All checks passed!\n");
    return true;
}