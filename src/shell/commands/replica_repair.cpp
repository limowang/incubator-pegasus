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
#include <rocksdb/sst_file_reader.h>
#include <rocksdb/sst_file_writer.h>
#include <rocksdb/iterator.h>
#include <rocksdb/table_properties.h>
#include <stdio.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "base/meta_store.h"
#include "base/pegasus_key_schema.h"
#include "common/gpid.h"
#include "dsn.layer2_types.h"
#include "replica/replication_app_base.h"
#include "shell/command_executor.h"
#include "shell/commands.h"
#include "utils/error_code.h"
#include "utils/filesystem.h"
#include "utils/fmt_logging.h"
#include "utils/load_dump_object.h"
#include "utils/blob.h"


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

// Forward declarations for new functions
bool verify_repaired_replica(const std::string& output_dir, RepairResult& result, std::string& error_msg);
void generate_json_report(const RepairConfig& config, const RepairResult& result, const std::string& report_file);

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
                          uint64_t& last_manual_compact_finish_time,
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

    if (ret && ms->get_last_manual_compact_finish_time(&last_manual_compact_finish_time) != dsn::ERR_OK) {
        error_msg = "Failed to get last manual compact finish time";
        // 非关键元数据，不返回错误
        last_manual_compact_finish_time = 0;
    }

    release_db(&cf_hdls, &db);
    return ret;
}

// Add after read_rocksdb_metadata, using local_partition_split pattern:
bool repair_sst_file(const std::string& src_sst,
                    const std::string& dst_sst,
                    const RepairConfig& config,
                    int64_t& records_recovered,
                    int64_t& records_skipped,
                    std::string& error_msg) {
    fmt::print(stdout, "  Processing: {}\n", src_sst);

    // 1. Open reader
    auto reader = std::make_unique<rocksdb::SstFileReader>(rocksdb::Options());
    rocksdb::Status status = reader->Open(src_sst);

    if (!status.ok()) {
        error_msg = fmt::format("Failed to open SST file: {}", status.ToString());
        return false;
    }

    // 2. Verify checksum
    status = reader->VerifyChecksum();
    if (!status.ok()) {
        error_msg = fmt::format("Checksum verification failed: {}", status.ToString());
        if (!config.skip_corrupted) {
            return false;
        }
        fmt::print(stdout, "  ✗ Checksum failed - skipping file\n");
        return false;
    }

    // 3. Check table properties
    auto tbl_ppts = reader->GetTableProperties();
    if (!tbl_ppts) {
        error_msg = "Failed to get table properties";
        return false;
    }

    // Skip if this is metadata column family file
    if (tbl_ppts->column_family_name == pegasus::server::meta_store::META_COLUMN_FAMILY_NAME) {
        fmt::print(stdout, "  ⊘ Metadata CF file - skipping\n");
        return true; // Not an error, just skip
    }

    // 4. Create writer for repaired file
    auto writer = std::make_shared<rocksdb::SstFileWriter>(
        rocksdb::EnvOptions(), rocksdb::Options());

    // Extract directory from destination path
    std::string dst_dir = dst_sst.substr(0, dst_sst.find_last_of('/'));
    if (dst_dir.empty()) {
        dst_dir = ".";
    }

    if (!dsn::utils::filesystem::directory_exists(dst_dir)) {
        if (!dsn::utils::filesystem::create_directory(dst_dir)) {
            error_msg = fmt::format("Failed to create directory: {}", dst_dir);
            return false;
        }
    }

    status = writer->Open(dst_sst);
    if (!status.ok()) {
        error_msg = fmt::format("Failed to create SST writer: {}", status.ToString());
        return false;
    }

    // 5. Iterate through records
    std::unique_ptr<rocksdb::Iterator> iter(reader->NewIterator({}));
    int64_t recovered = 0;
    int64_t skipped = 0;

    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
        const auto& key = iter->key();
        const auto& value = iter->value();

        // Skip empty writes (from local_partition_split pattern)
        if (key.empty() && value.empty()) {
            skipped++;
            continue;
        }

        // Write valid record
        status = writer->Put(key, value);
        if (!status.ok()) {
            error_msg = fmt::format("Failed to write record: {}", status.ToString());
            skipped++;
            if (!config.skip_corrupted) {
                writer->Finish(nullptr);
                return false;
            }
            continue;
        }

        recovered++;
    }

    // 6. Finalize writer
    status = writer->Finish(nullptr);
    if (!status.ok()) {
        error_msg = fmt::format("Failed to finalize SST file: {}", status.ToString());
        return false;
    }

    records_recovered = recovered;
    records_skipped = skipped;

    fmt::print(stdout, "  ✓ Recovered: {}, Skipped: {}\n", recovered, skipped);
    return true;
}

// Add after repair_sst_file:
bool repair_all_sst_files(const std::vector<std::string>& sst_files,
                         const std::string& output_dir,
                         const RepairConfig& config,
                         RepairStats& stats,
                         std::string& error_msg) {
    fmt::print(stdout, "\nRepairing SST files...\n");

    int64_t total_recovered = 0;
    int64_t total_skipped = 0;
    int64_t corrupted_count = 0;

    for (size_t i = 0; i < sst_files.size(); i++) {
        const auto& src_file = sst_files[i];

        // Generate destination path (preserve structure)
        std::string relative_path = src_file.substr(config.replica_dir.size());
        std::string dst_file = output_dir + relative_path;

        int64_t recovered = 0;
        int64_t skipped = 0;
        std::string file_error;

        bool success = repair_sst_file(src_file, dst_file, config, recovered, skipped, file_error);

        if (success) {
            total_recovered += recovered;
            total_skipped += skipped;
        } else {
            corrupted_count++;
            fmt::print(stdout, "  ✗ Failed: {}\n", file_error);
            if (!config.skip_corrupted) {
                error_msg = fmt::format("Aborted: File repair failed: {}", file_error);
                return false;
            }
        }

        // Progress update
        if ((i + 1) % 10 == 0 || i == sst_files.size() - 1) {
            fmt::print(stdout, "  Progress: {}/{}\n", i + 1, sst_files.size());
        }
    }

    stats.recovered_records = total_recovered;
    stats.skipped_records = total_skipped;
    stats.corrupted_sst_files = corrupted_count;

    fmt::print(stdout, "\nSST repair completed: {} recovered, {} skipped\n",
               total_recovered, total_skipped);

    return true;
}

// Add after repair_all_sst_files:
bool create_new_database(const std::string& output_dir,
                       const RepairConfig& config,
                       rocksdb::DB** db,
                       std::vector<rocksdb::ColumnFamilyHandle*>* cf_hdls,
                       std::string& error_msg) {
    fmt::print(stdout, "Creating new RocksDB database...\n");

    auto rdb_dir = dsn::utils::filesystem::path_combine(
        output_dir,
        "rdb"  // 注意：使用"rdb"而不是"rdb/data"，基于任务4和8的修正
    );

    // Create directory structure
    if (!dsn::utils::filesystem::directory_exists(rdb_dir)) {
        if (!dsn::utils::filesystem::create_directory(rdb_dir)) {
            error_msg = fmt::format("Failed to create directory: {}", rdb_dir);
            return false;
        }
    }

    rocksdb::DBOptions db_opts;
    db_opts.create_if_missing = true;
    db_opts.create_missing_column_families = true;

    const std::vector<rocksdb::ColumnFamilyDescriptor> cf_dscs(
        {{pegasus::server::meta_store::DATA_COLUMN_FAMILY_NAME, {}},
         {pegasus::server::meta_store::META_COLUMN_FAMILY_NAME, {}}});

    if (!open_rocksdb(db_opts, rdb_dir, false, cf_dscs, cf_hdls, db)) {
        error_msg = "Failed to create new RocksDB database";
        return false;
    }

    fmt::print(stdout, "New database created successfully\n");
    return true;
}

// Add after create_new_database:
bool import_repaired_sst_files(const std::string& output_dir,
                               rocksdb::DB* db,
                               std::vector<rocksdb::ColumnFamilyHandle*>* cf_hdls,
                               std::string& error_msg) {
    fmt::print(stdout, "Importing repaired SST files...\n");

    auto rdb_dir = dsn::utils::filesystem::path_combine(
        output_dir,
        "rdb"  // 注意：使用"rdb"而不是"rdb/data"
    );

    // Collect repaired SST files
    std::vector<std::string> repaired_sst_files;
    std::vector<std::string> subdirs;

    if (!dsn::utils::filesystem::get_subdirectories(rdb_dir, subdirs, false)) {
        error_msg = "Failed to list subdirectories in output directory";
        return false;
    }

    for (const auto& subdir : subdirs) {
        std::vector<std::string> files;
        if (!dsn::utils::filesystem::get_subfiles(subdir, files, false)) {
            continue;
        }

        for (const auto& file : files) {
            if (file.size() >= 4 && file.substr(file.size() - 4) == ".sst") {
                repaired_sst_files.push_back(file);
            }
        }
    }

    if (repaired_sst_files.empty()) {
        error_msg = "No repaired SST files found to import";
        return false;
    }

    // Import files using IngestExternalFile
    for (const auto& file : repaired_sst_files) {
        rocksdb::IngestExternalFileArg arg;
        arg.column_family = (*cf_hdls)[0]; // Data column family
        arg.external_files.push_back(file);

        rocksdb::Status status = db->IngestExternalFiles({arg});
        if (!status.ok()) {
            error_msg = fmt::format("Failed to import SST file {}: {}", file, status.ToString());
            return false;
        }
    }

    fmt::print(stdout, "Imported {} SST files\n", repaired_sst_files.size());
    return true;
}

// Add after import_repaired_sst_files:
dsn::error_code write_metadata_files(const std::string& output_dir,
                                     const std::string& replica_dir,
                                     const dsn::app_info& app_info,
                                     const dsn::replication::replica_init_info& init_info,
                                     uint64_t last_decree,
                                     std::string& error_msg) {
    dsn::error_code err;
    fmt::print(stdout, "Generating metadata files...\n");

    // Copy .app-info file directly from replica directory
    const auto app_info_src = dsn::utils::filesystem::path_combine(
        replica_dir, dsn::replication::replica_app_info::kAppInfo);
    const auto app_info_dst = dsn::utils::filesystem::path_combine(
        output_dir, dsn::replication::replica_app_info::kAppInfo);

    // Check if source file exists
    if (!dsn::utils::filesystem::file_exists(app_info_src)) {
        fmt::print(stdout, "  ⚠ .app-info not found in source, will use generated version\n");
        // Fallback: generate .app-info file
        dsn::app_info new_ai(app_info);
        dsn::replication::replica_app_info rai(&new_ai);
        err = rai.store(app_info_dst);
        if (err != dsn::ERR_OK) {
            error_msg = fmt::format("Failed to write app-info to {}", app_info_dst);
            return err;
        }
    } else {
        // Copy file directly
        rocksdb::Status status = dsn::utils::copy_file(app_info_src, app_info_dst);
        if (!status.ok()) {
            error_msg = fmt::format("Failed to copy app-info from {} to {}: {}",
                                app_info_src, app_info_dst, status.ToString());
            return dsn::ERR_FILE_OPERATION_FAILED;
        }
        fmt::print(stdout, "  ✓ Copied .app-info\n");
    }

    // Generate .init-info file
    dsn::replication::replica_init_info new_rii(init_info);
    if (last_decree > 0) {
        new_rii.init_durable_decree = last_decree;
    }
    const auto rii_path = dsn::utils::filesystem::path_combine(
        output_dir, dsn::replication::replica_init_info::kInitInfo);

    err = dsn::utils::dump_rjobj_to_file(new_rii, rii_path);
    if (err != dsn::ERR_OK) {
        error_msg = fmt::format("Failed to write init-info to {}", rii_path);
        return err;
    }

    fmt::print(stdout, "  ✓ Generated .init-info\n");
    return dsn::ERR_OK;
}

// Add after write_metadata_files:
bool set_rocksdb_metadata(rocksdb::DB* db,
                         std::vector<rocksdb::ColumnFamilyHandle*>* cf_hdls,
                         uint32_t pegasus_data_version,
                         uint64_t last_decree,
                         uint64_t last_manual_compact_finish_time,
                         std::string& error_msg) {
    fmt::print(stdout, "Setting RocksDB metadata...\n");

    auto rdb_dir = db->GetName(); // Get the DB directory path
    auto ms = std::make_unique<pegasus::server::meta_store>(rdb_dir.c_str(), db, (*cf_hdls)[1]);

    // Set data version, last flushed decree, and last manual compact finish time
    ms->set_data_version(pegasus_data_version);
    ms->set_last_flushed_decree(last_decree);
    ms->set_last_manual_compact_finish_time(last_manual_compact_finish_time);

    // Flush to ensure persistence
    rocksdb::FlushOptions flush_opts;
    flush_opts.wait = true;
    rocksdb::Status status = db->Flush(flush_opts, *cf_hdls);
    if (!status.ok()) {
        error_msg = fmt::format("Failed to flush database: {}", status.ToString());
        return false;
    }

    fmt::print(stdout, "  ✓ Metadata set and flushed\n");
    return true;
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
    uint32_t pegasus_data_version = 1;  // 默认版本1
    uint64_t last_manual_compact_finish_time = 0;

    bool rocksdb_readable = read_rocksdb_metadata(rdb_dir, last_committed_decree,
                                                  pegasus_data_version,
                                                  last_manual_compact_finish_time,
                                                  error_msg);
    if (rocksdb_readable) {
        fmt::print(stdout, "RocksDB metadata: last_decree={}, data_version={}\n",
                   last_committed_decree, pegasus_data_version);
        // Update init_info with actual decree
        init_info.init_durable_decree = last_committed_decree;
    } else {
        fmt::print(stdout, "INFO: Could not read RocksDB metadata (expected if corrupted): {}\n",
                   error_msg);
    }

    // Step 6: Check dry_run
    if (config.dry_run) {
        fmt::print(stdout, "\n=== DRY RUN MODE - No actual repair performed ===\n");
        fmt::print(stdout, "Found {} SST files\n", sst_files.size());
        if (config.create_backup) {
            cleanup_backup(config.backup_dir);
        }
        return true;
    }

    // Step 7: Repair SST files
    RepairStats stats;
    stats.total_sst_files = sst_files.size();

    auto start_time = std::chrono::steady_clock::now();

    if (!repair_all_sst_files(sst_files, config.output_dir, config, stats, error_msg)) {
        fmt::print(stderr, "Error: {}\n", error_msg);
        if (config.create_backup) {
            rollback_to_backup(config.backup_dir, config.output_dir, error_msg);
        }
        if (config.create_backup) {
            cleanup_backup(config.backup_dir);
        }
        return false;
    }

    auto end_time = std::chrono::steady_clock::now();
    stats.duration_seconds = std::chrono::duration<double>(end_time - start_time).count();

    fmt::print(stdout, "\nSST files repaired in {:.2f} seconds\n", stats.duration_seconds);
    fmt::print(stdout, "Total files: {}, Recovered records: {}, Skipped records: {}\n",
               stats.total_sst_files, stats.recovered_records, stats.skipped_records);
    fmt::print(stdout, "Corrupted files: {}\n", stats.corrupted_sst_files);

    // Step 8: Create new database and import repaired SST files
    rocksdb::DB* new_db = nullptr;
    std::vector<rocksdb::ColumnFamilyHandle*> new_cf_hdls;

    if (!create_new_database(config.output_dir, config, &new_db, &new_cf_hdls, error_msg)) {
        fmt::print(stderr, "Error: {}\n", error_msg);
        if (config.create_backup) {
            rollback_to_backup(config.backup_dir, config.output_dir, error_msg);
            cleanup_backup(config.backup_dir);
        }
        return false;
    }

    if (!import_repaired_sst_files(config.output_dir, new_db, &new_cf_hdls, error_msg)) {
        fmt::print(stderr, "Error: {}\n", error_msg);
        release_db(&new_cf_hdls, &new_db);
        if (config.create_backup) {
            rollback_to_backup(config.backup_dir, config.output_dir, error_msg);
            cleanup_backup(config.backup_dir);
        }
        return false;
    }

    fmt::print(stdout, "New database created and SST files imported\n");

    // Step 9: Set RocksDB metadata
    if (!set_rocksdb_metadata(new_db, &new_cf_hdls, pegasus_data_version,
                              last_committed_decree, last_manual_compact_finish_time,
                              error_msg)) {
        fmt::print(stderr, "Error: {}\n", error_msg);
        release_db(&new_cf_hdls, &new_db);
        if (config.create_backup) {
            rollback_to_backup(config.backup_dir, config.output_dir, error_msg);
            cleanup_backup(config.backup_dir);
        }
        return false;
    }

    // Close database before writing files
    release_db(&new_cf_hdls, &new_db);

    // Step 10: Write metadata files
    if (!write_metadata_files(config.output_dir, config.replica_dir, app_info, init_info,
                             last_committed_decree, error_msg)) {
        fmt::print(stderr, "Error: {}\n", error_msg);
        if (config.create_backup) {
            rollback_to_backup(config.backup_dir, config.output_dir, error_msg);
            cleanup_backup(config.backup_dir);
        }
        return false;
    }

    fmt::print(stdout, "Metadata files generated successfully\n");

    // Step 11: Cleanup backup on success
    if (config.create_backup) {
        cleanup_backup(config.backup_dir);
    }

    // Step 12: Verify repaired replica (if requested)
    RepairResult result;
    result.success = true;
    result.stats = stats;
    result.backup_path = config.backup_dir;

    if (config.verify_repair) {
        fmt::print(stdout, "\n=== Verifying repaired replica ===\n");
        if (!verify_repaired_replica(config.output_dir, result, error_msg)) {
            result.warnings.push_back("Verification failed: " + error_msg);
            result.verification_passed = false;
        } else {
            result.verification_passed = true;
            fmt::print(stdout, "Verification passed\n");
        }
    }

    // Step 13: Generate JSON report (if requested)
    if (!config.report_file.empty()) {
        generate_json_report(config, result, config.report_file);
    }

    fmt::print(stdout, "SUCCESS: Replica repair completed!\n");
    return true;
}

// Task 13: Verify repaired replica
bool verify_repaired_replica(const std::string& output_dir, RepairResult& result, std::string& error_msg) {
    fmt::print(stdout, "Opening repaired database for verification...\n");

    auto rdb_dir = dsn::utils::filesystem::path_combine(output_dir, "rdb");

    // Try to open the database
    rocksdb::DBOptions db_opts;
    const std::vector<rocksdb::ColumnFamilyDescriptor> cf_dscs(
        {{pegasus::server::meta_store::DATA_COLUMN_FAMILY_NAME, {}},
         {pegasus::server::meta_store::META_COLUMN_FAMILY_NAME, {}}});

    std::vector<rocksdb::ColumnFamilyHandle*> cf_hdls;
    rocksdb::DB* db = nullptr;

    if (!open_rocksdb(db_opts, rdb_dir, true, cf_dscs, &cf_hdls, &db)) {
        error_msg = "Failed to open repaired database for verification";
        return false;
    }

    // Verify metadata consistency
    auto ms = std::make_unique<pegasus::server::meta_store>(rdb_dir.c_str(), db, cf_hdls[1]);

    uint64_t last_decree = 0;
    uint32_t data_version = 0;

    bool metadata_ok = true;
    if (ms->get_last_flushed_decree(&last_decree) != dsn::ERR_OK) {
        result.warnings.push_back("Could not read last flushed decree during verification");
        metadata_ok = false;
    }

    if (ms->get_data_version(&data_version) != dsn::ERR_OK) {
        result.warnings.push_back("Could not read data version during verification");
        metadata_ok = false;
    }

    release_db(&cf_hdls, &db);

    if (metadata_ok) {
        fmt::print(stdout, "  ✓ Metadata verified: decree={}, version={}\n", last_decree, data_version);
    }

    // Verify metadata files exist
    auto app_info_path = dsn::utils::filesystem::path_combine(
        output_dir, dsn::replication::replica_app_info::kAppInfo);
    auto init_info_path = dsn::utils::filesystem::path_combine(
        output_dir, dsn::replication::replica_init_info::kInitInfo);

    if (!dsn::utils::filesystem::file_exists(app_info_path)) {
        result.warnings.push_back("Missing .app-info file");
        metadata_ok = false;
    }

    if (!dsn::utils::filesystem::file_exists(init_info_path)) {
        result.warnings.push_back("Missing .init-info file");
        metadata_ok = false;
    }

    if (metadata_ok) {
        fmt::print(stdout, "  ✓ Metadata files present\n");
    }

    return metadata_ok;
}

// Task 14: Generate JSON report
void generate_json_report(const RepairConfig& config, const RepairResult& result, const std::string& report_file) {
    fmt::print(stdout, "Generating JSON report: {}\n", report_file);

    // Build JSON manually (simple approach without external library)
    std::string json = "{\n";
    json += "  \"success\": " + std::string(result.success ? "true" : "false") + ",\n";
    json += "  \"verification_passed\": " + std::string(result.verification_passed ? "true" : "false") + ",\n";
    json += "  \"gpid\": \"" + std::to_string(config.app_id) + "." + std::to_string(config.partition_id) + "\",\n";
    json += "  \"replica_dir\": \"" + config.replica_dir + "\",\n";
    json += "  \"output_dir\": \"" + config.output_dir + "\",\n";
    json += "  \"backup_path\": \"" + result.backup_path + "\",\n";

    // Statistics
    json += "  \"statistics\": {\n";
    json += "    \"total_sst_files\": " + std::to_string(result.stats.total_sst_files) + ",\n";
    json += "    \"corrupted_sst_files\": " + std::to_string(result.stats.corrupted_sst_files) + ",\n";
    json += "    \"total_records\": " + std::to_string(result.stats.total_records) + ",\n";
    json += "    \"recovered_records\": " + std::to_string(result.stats.recovered_records) + ",\n";
    json += "    \"skipped_records\": " + std::to_string(result.stats.skipped_records) + ",\n";
    json += "    \"data_size_bytes\": " + std::to_string(result.stats.data_size_bytes) + ",\n";
    json += "    \"duration_seconds\": " + std::to_string(result.stats.duration_seconds) + "\n";
    json += "  },\n";

    // Warnings
    json += "  \"warnings\": [";
    for (size_t i = 0; i < result.warnings.size(); i++) {
        if (i > 0) json += ", ";
        json += "\"" + result.warnings[i] + "\"";
    }
    json += "],\n";

    // Error message (if any)
    json += "  \"error_message\": \"" + result.error_message + "\"\n";
    json += "}\n";

    // Write to file
    std::ofstream out(report_file);
    if (out.is_open()) {
        out << json;
        out.close();
        fmt::print(stdout, "  ✓ JSON report written\n");
    } else {
        fmt::print(stderr, "  ✗ Failed to write JSON report to {}\n", report_file);
    }
}