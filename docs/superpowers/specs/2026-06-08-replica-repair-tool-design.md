# Pegasus Replica Repair Tool - Design Specification

**Date**: 2026-06-08
**Author**: Claude Code
**Status**: Draft (v1.1 - Revised based on feedback)
**Topic**: Replica Data Repair Tool for RocksDB Corruption Recovery

## 1. Overview

### 1.1 Problem Statement

During rolling upgrades, Pegasus replicas may encounter RocksDB corruption errors that prevent them from starting. Common errors include:

- `Corruption: force_consistency_checks: VersionBuilder: L3 has overlapping ranges`
- `Corruption: Compaction sees out-of-order keys`

Existing RocksDB repair tools are insufficient for these complex corruption scenarios. We need a dedicated tool that can:

1. Reconstruct replica data from existing SST files
2. Handle various types of corruption gracefully
3. Generate required metadata files (`.app-info`, `.init-info`)
4. Provide detailed progress reporting and diagnostics

### 1.2 Solution Approach

**Approach**: SST File Scanning and Reconstruction (Method B)

Implement as a Pegasus shell command (`replica_repair`) that:
- Uses `rocksdb::SstFileReader` to scan existing SST files
- Validates and recovers data from corrupted files
- Creates a new database instance with recovered data
- Generates required metadata files
- Provides detailed progress output and JSON reports
- Includes backup and verification mechanisms

**Key Design Decision**: Single replica repair mode requiring explicit parameters (gpid, replica_dir, output_dir).

## 2. Architecture and Components

### 2.1 Tool Architecture

```
replica_repair (shell command)
├── Command-line Interface
├── Backup Module (NEW)
│   ├── Replica Directory Backup
│   └── Backup Integrity Verification
├── Diagnosis Module
│   ├── Concrete Corruption Type Detection (ENHANCED)
│   ├── SST File Scanner
│   └── Recoverability Assessment
├── Repair Engine
│   ├── SST Reader
│   ├── Data Validator
│   └── New Database Writer
├── Metadata Generator
│   ├── .app-info Generator
│   └── .init-info Generator
├── Verification Module (NEW)
│   ├── Repaired Database Open Test
│   ├── Metadata Consistency Check
│   └── Sample Data Verification
├── Report Generator
│   ├── Progress Output
│   └── JSON Report
└── Error Handler
```

### 2.2 Core Components

#### 2.2.1 Main Entry Point
- Parse command-line arguments
- Validate input parameters
- Coordinate module execution

#### 2.2.2 Backup Module (NEW)
- Create complete backup of original replica directory
- Verify backup integrity before proceeding
- Store backup path for rollback if needed
- Clean up backup on successful repair

#### 2.2.3 Diagnosis Module
- Detect specific corruption types using concrete detection logic:
  - `MANIFEST_CORRUPTED`: MANIFEST file unreadable or invalid
  - `SST_CHECKSUM_FAILED`: Individual SST file checksum fails
  - `KEY_ORDER_CORRUPTED`: Keys not in proper order
  - `METADATA_CORRUPTED`: .app-info or .init-info corrupted
  - `UNKNOWN`: Undetermined corruption type
- Scan replica directory, collect all SST file information
- Assess recoverability based on error thresholds
- Estimate recoverable data volume

#### 2.2.4 Repair Engine
- Use `rocksdb::SstFileReader` to read each SST file
- Validate each record including Pegasus key schema (hashkey + sortkey)
- Validate composite key format during repair process
- Create new database instance, write valid data
- Skip corrupted records based on error thresholds

#### 2.2.5 Metadata Generator
- Generate `.app-info` file with application information
- Generate `.init-info` file with initialization state
- Handle both existing metadata (from original replica) and default values

#### 2.2.6 Verification Module (NEW)
- Try to open the repaired database to ensure it's valid
- Verify metadata consistency between files and database
- Perform sample data verification (check random keys)
- Return verification status and any issues found

#### 2.2.7 Report Generator
- Real-time progress output with granular tracking:
  - Files processed vs. total files
  - Records recovered count
  - Bytes processed
  - Current operation status
- Generate JSON format detailed report (before/after comparison, data statistics)

#### 2.2.8 Error Handler
- Log all skipped records with detailed reasons
- Provide concrete error recovery strategies based on thresholds
- Support rollback to backup on critical failures
- Provide detailed error messages and user guidance

## 3. Data Structures

### 3.1 Command-line Interface

```bash
# Via Pegasus shell
./run.sh shell
> repair_replica <gpid> <replica_dir> <output_dir> [options]

# Or directly
./pegasus_shell --cluster meta.server.host:34601 \
    -c "repair_replica 4.27 /path/to/replica /path/to/output --report_file report.json"
```

**Parameters**:
- `<gpid>`: Global partition ID (format: `<app_id>.<partition_id>`, e.g., `4.27`)
- `<replica_dir>`: Corrupted replica directory path
- `<output_dir>`: New replica output directory path
- `--backup_dir <path>`: Backup directory path (optional, auto-generated if not specified)
- `--no_backup`: Skip backup creation (NOT recommended)
- `--report_file <path>`: JSON report file path (optional, default: stdout)
- `--dry_run`: Diagnose only without actual repair
- `--skip_corrupted_records`: Skip corrupted records and continue repair
- `--max_corrupted_ratio <0.0-1.0>`: Maximum corrupted file ratio (default: 0.5)
- `--verify_repair`: Verify repaired replica (default: true)
- `--help`: Display help information

### 3.2 Core Data Structures

```cpp
// Repair configuration
struct RepairConfig {
    int32_t app_id;                    // From gpid
    int32_t partition_id;              // From gpid
    std::string replica_dir;           // Original replica directory
    std::string output_dir;            // Output directory
    std::string backup_dir;            // Backup directory
    std::string report_file;           // Report file path
    bool dry_run;                      // Diagnose only mode
    bool skip_corrupted;               // Skip corrupted records
    bool create_backup;                // Create backup before repair
    bool verify_repair;                // Verify repaired replica
    double max_corrupted_ratio;        // Max corrupted file ratio (0.5)
    dsn::logging::log_level log_level; // Log level
};

// Corruption type detection (NEW)
enum class CorruptionType {
    MANIFEST_CORRUPTED,     // MANIFEST file unreadable
    SST_CHECKSUM_FAILED,    // Individual SST file checksum fails
    KEY_ORDER_CORRUPTED,    // Keys not in proper order
    METADATA_CORRUPTED,     // .app-info or .init-info corrupted
    UNKNOWN                 // Undetermined corruption type
};

// Error thresholds (NEW)
struct ErrorThresholds {
    double max_corrupted_file_ratio = 0.5;      // Abort if >50% files corrupted
    int64_t max_skipped_records = 1000000;       // Warn if >1M records skipped
    double min_recoverable_data_ratio = 0.1;     // Abort if <10% data recoverable
};

// SST file information
struct SstFileInfo {
    std::string file_path;             // File path
    int64_t file_size;                 // File size
    int32_t level;                     // Level (L0, L1, ...)
    int64_t record_count;              // Record count
    std::string smallest_key;          // Smallest key
    std::string largest_key;           // Largest key
    bool is_corrupted;                 // Is corrupted
};

// Repair statistics
struct RepairStats {
    int64_t total_sst_files;           // Total SST files
    int64_t corrupted_sst_files;       // Corrupted SST files
    int64_t total_records;             // Total records
    int64_t recovered_records;         // Recovered records
    int64_t skipped_records;           // Skipped records
    int64_t data_size_bytes;           // Recovered data size
    double duration_seconds;           // Repair duration
};

// Repair result
struct RepairResult {
    bool success;                      // Success flag
    std::string error_message;         // Error message
    RepairStats stats;                 // Statistics
    std::vector<std::string> warnings; // Warnings
};
```

### 3.3 Metadata Structures

```cpp
// Replica metadata
struct ReplicaMetadata {
    dsn::app_info app;                 // Application information
    dsn::replication::replica_init_info init_info; // Initialization info

    // Load from original replica
    static dsn::error_code load_from_original(
        const std::string& replica_dir,
        ReplicaMetadata& metadata);

    // Write to repaired replica
    dsn::error_code write_to_repair(const std::string& output_dir) const;
};
```

## 4. Implementation Details

### 4.1 File Structure

```
src/shell/commands/
└── replica_repair.cpp          # Main implementation

src/shell/
├── main.cpp                    # Register new command
└── commands.h                  # Command declaration

# Command registration in commands.h
extern const std::string replica_repair_help;
bool replica_repair(command_executor *e, shell_context *sc, arguments args);

# Command registration in main.cpp
{
    "repair_replica",
    "Repair corrupted Pegasus replicas by rebuilding from SST files",
    "<gpid> <replica_dir> <output_dir> [--backup_dir path] [--no_backup] "
    "[--report_file path] [--dry_run] [--skip_corrupted_records] "
    "[--max_corrupted_ratio ratio] [--verify_repair] [--help]",
    repair_replica,
}
```

### 4.2 Key Dependencies

**RocksDB Dependencies**:
```cpp
rocksdb::DB
rocksdb::SstFileReader
rocksdb::SstFileWriter
rocksdb::ThreadPool
```

**Pegasus Internal Dependencies**:
```cpp
pegasus::server::meta_store              // Metadata management
dsn::replication::replica_app_info       // .app-info file
dsn::replication::replica_init_info      // .init-info file
dsn::utils::filesystem                  // File system operations
dsn::utils::load_rjobj_from_file        // JSON reading
dsn::utils::dump_rjobj_to_file          // JSON writing
```

### 4.3 Implementation Patterns (from local_partition_split)

**SST File Processing**:
```cpp
// Read using SstFileReader
auto reader = std::make_unique<rocksdb::SstFileReader>(rocksdb::Options());
reader->Open(src_sst_file);
reader->VerifyChecksum();

// Write using SstFileWriter
auto writer = std::make_shared<rocksdb::SstFileWriter>(
    rocksdb::EnvOptions(), rocksdb::Options());
writer->Open(dst_sst_file);
writer->Put(key, value);
writer->Finish();

// Import SST files
rocksdb::IngestExternalFileArg arg;
arg.column_family = data_cf_handle;
db->IngestExternalFiles({arg});
```

**Metadata Management**:
```cpp
// Use meta_store for metadata management
auto ms = std::make_unique<pegasus::server::meta_store>(
    rdb_dir.c_str(), db, meta_cf_handle);

// Read metadata
uint64_t last_committed_decree;
ms->get_last_flushed_decree(&last_committed_decree);

uint32_t pegasus_data_version;
ms->get_data_version(&pegasus_data_version);

// Write metadata
new_ms->set_data_version(pegasus_data_version);
new_ms->set_last_flushed_decree(last_committed_decree);
db->Flush(options, cf_handles);
```

**Metadata File Generation**:
```cpp
// Generate .app-info file
dsn::app_info new_ai(original_app_info);
new_ai.app_id = app_id;
new_ai.partition_count = partition_count;
dsn::replication::replica_app_info rai(&new_ai);
const auto rai_path = dsn::utils::filesystem::path_combine(
    replica_dir, dsn::replication::replica_app_info::kAppInfo);
rai.store(rai_path);

// Generate .init-info file
dsn::replication::replica_init_info new_rii;
new_rii.init_ballot = 0;
new_rii.init_durable_decree = last_durable_decree;
new_rii.init_offset_in_private_log = 0;
const auto rii_path = dsn::utils::filesystem::path_combine(
    replica_dir, dsn::replication::replica_init_info::kInitInfo);
dsn::utils::dump_rjobj_to_file(new_rii, rii_path);
```

### 4.4 Repair Flow

```
0. Create backup of original replica (NEW)
   ├── Copy entire replica directory to backup location
   ├── Verify backup integrity
   └── Store backup path for potential rollback

1. Parse and validate parameters
   ├── Parse gpid, replica_dir, output_dir
   └── Validate directory existence and permissions

2. Detect corruption type (NEW)
   ├── Attempt to open MANIFEST file
   ├── Check SST file checksums
   ├── Verify key ordering in SST files
   ├── Validate metadata files
   └── Determine specific corruption type

3. Load metadata from original replica
   ├── Try to read .app-info file
   ├── Try to read .init-info file
   └── Use default values if files are corrupted

4. Open original RocksDB in read-only mode (if possible)
   ├── Create column family handles (data and meta)
   └── Read metadata using meta_store

5. Scan all SST files
   ├── Get live files metadata
   ├── Skip meta column family files
   └── Collect file information and check corruption ratios

6. Check error thresholds (NEW)
   ├── Calculate corrupted file ratio
   ├── Abort if exceeds max_corrupted_ratio
   ├── Estimate recoverable data ratio
   └── Warn if exceeds thresholds

7. Create new RocksDB instance
   ├── Create new database directory
   ├── Open new database (create_if_missing = true)
   └── Create required column families

8. Repair SST files
   ├── For each SST file:
   │   ├── Open with SstFileReader
   │   ├── Verify checksum
   │   ├── Iterate through records
   │   ├── Validate Pegasus key format (hashkey + sortkey)
   │   ├── Write valid records to new SST file
   │   └── Skip corrupted records (if enabled)
   └── Close all writers

9. Import repaired SST files
   ├── Use IngestExternalFile
   └── Optional: full compaction

10. Set metadata to new database
    ├── Use meta_store to set metadata
    └── Flush to ensure persistence

11. Generate metadata files
    ├── Generate .app-info file
    └── Generate .init-info file

12. Verify repaired replica (NEW)
    ├── Try to open the repaired database
    ├── Verify metadata consistency
    ├── Perform sample data verification
    └── Report verification status

13. Cleanup on success (NEW)
    ├── Remove backup if verification passed
    └── Generate final repair report

14. Rollback on failure (NEW)
    ├── Restore from backup
    ├── Cleanup partial repair
    └── Report failure with details
```

## 5. Error Handling

### 5.1 Error Classification

**File-level Errors**:
```cpp
enum class FileErrorType {
    OK,
    FILE_NOT_FOUND,
    CHECKSUM_FAILED,
    CORRUPTED_DATA,
    INVALID_FORMAT,
    PERMISSION_DENIED
};

// Recovery actions for file errors
enum class FileErrorAction {
    SKIP_FILE,              // Skip this file, continue with others
    SKIP_RECORDS,           // Skip corrupted records, continue with file
    ABORT_REPAIR,           // Abort entire repair process
    RETRY_WITH_FALLBACK     // Try alternative approach
};
```

**Metadata Errors**:
```cpp
enum class MetadataErrorType {
    OK,
    APP_INFO_MISSING,
    APP_INFO_CORRUPTED,
    INIT_INFO_MISSING,
    INIT_INFO_CORRUPTED,
    MANIFEST_CORRUPTED
};
```

### 5.2 Error Handling Strategy (ENHANCED)

**Concrete Recovery Strategies**:

1. **Checksum Failed**:
   - Action: `SKIP_RECORDS` if `--skip_corrupted_records` enabled
   - Fallback: `SKIP_FILE` if too many records corrupted
   - Threshold: Warn if >1M records skipped

2. **Corrupted Data**:
   - Action: Try to skip corrupted records, continue processing
   - Validation: Check Pegasus key format (hashkey + sortkey)
   - Fallback: `SKIP_FILE` if corruption ratio >50%

3. **File Not Found**:
   - Action: Warning and `SKIP_FILE`
   - Recovery: Continue with remaining files

4. **Permission Denied**:
   - Action: `ABORT_REPAIR`
   - Guidance: Provide specific permission fix instructions

5. **Metadata File Missing**:
   - Action: Use default values + warning
   - Defaults: app_id from gpid, partition_count from config

6. **Metadata File Corrupted**:
   - Action: Try to salvage fields, use default values + warning
   - Recovery: Cross-reference with RocksDB metadata

7. **MANIFEST Corrupted**:
   - Action: This is the primary case this tool handles
   - Strategy: Rebuild from SST files (main repair flow)

8. **High Corruption Ratio** (>50% files):
   - Action: `ABORT_REPAIR` with clear warning
   - Guidance: Suggest alternative recovery methods

9. **Low Recoverable Data** (<10%):
   - Action: `ABORT_REPAIR` with warning
   - Guidance: Data may be irrecoverable

### 5.3 Boundary Cases

**Empty Replica**:
```cpp
if (files.empty() && !metadata_exists) {
    result.error_message = "Replica is empty, no data to recover";
    result.success = false;
    return false;
}
```

**Partial Corruption**:
```cpp
double corrupted_ratio = static_cast<double>(corrupted_files.size()) / total_files;
if (corrupted_ratio > config.max_corrupted_ratio) {
    result.error_message = fmt::format(
        "Aborted: Corrupted file ratio {} exceeds threshold {}",
        corrupted_ratio, config.max_corrupted_ratio);
    result.success = false;
    return false;
}
if (corrupted_ratio > 0.3) {
    result.warnings.push_back(
        fmt::format("High corruption ratio detected: {}/{} files corrupted",
                   corrupted_files.size(), total_files));
}
```

**Insufficient Disk Space**:
```cpp
// Calculate required space with overhead factor
int64_t required_space = calculate_required_space(original_replica_dir);
int64_t backup_space = calculate_backup_space(original_replica_dir);
int64_t total_required = required_space + backup_space;
int64_t available_space = get_available_disk_space(output_dir);

if (available_space < total_required * 1.5) {  // 1.5x safety margin
    result.error_message = fmt::format(
        "Insufficient disk space: required={}MB, available={}MB",
        total_required / 1024 / 1024,
        available_space / 1024 / 1024);
    result.success = false;
    return false;
}
```

**Verification Failure** (NEW):
```cpp
if (!verify_repair(output_dir, verification_issues)) {
    result.warnings.push_back("Verification failed");
    result.verification_issues = verification_issues;
    if (config.create_backup) {
        // Rollback to backup
        rollback_to_backup(backup_dir, replica_dir);
        result.error_message = "Repair verification failed, rolled back to backup";
        result.success = false;
    }
    return false;
}
```

## 6. Reporting

### 6.1 Progress Output

```cpp
// Enhanced progress tracking (NEW)
struct RepairProgress {
    int64_t files_processed = 0;
    int64_t total_files = 0;
    int64_t records_recovered = 0;
    int64_t bytes_processed = 0;
    double current_file_progress = 0.0;  // 0.0 to 1.0
    std::string current_operation;       // Human-readable status

    // Progress callbacks
    std::function<void(const std::string&)> on_start_file;
    std::function<void(const std::string&, int64_t)> on_file_complete;
    std::function<void(const std::string&, const std::string&)> on_file_skipped;
    std::function<void(const RepairProgress&)> on_progress_update;
};

class RepairProgressTracker {
public:
    void start_operation(const std::string& operation);
    void on_start_file(const std::string& filename);
    void on_file_complete(const std::string& filename, int64_t records);
    void on_file_skipped(const std::string& filename, const std::string& reason);
    void on_progress_update(const RepairProgress& progress);
    void finish_operation(bool success, const std::string& message);
};
```

### 6.2 JSON Report Format

```json
{
  "gpid": "4.27",
  "timestamp": "2026-06-08T10:30:00Z",
  "success": true,
  "config": {
    "replica_dir": "/path/to/original/replica",
    "output_dir": "/path/to/repaired/replica",
    "backup_dir": "/tmp/replica_backup/4.27",
    "backup_created": true,
    "max_corrupted_ratio": 0.5
  },
  "diagnosis": {
    "corruption_type": "MANIFEST_CORRUPTED",
    "total_sst_files": 150,
    "corrupted_sst_files": 2,
    "corrupted_file_ratio": 0.013,
    "estimated_recoverable_records": 1500000
  },
  "statistics": {
    "total_records": 1520000,
    "recovered_records": 1498000,
    "skipped_records": 22000,
    "data_size_mb": 850.5,
    "duration_seconds": 245.8,
    "verification_passed": true
  },
  "verification": {
    "passed": true,
    "database_opened": true,
    "metadata_consistent": true,
    "sample_data_verified": true,
    "issues": []
  },
  "skipped_files": [
    {
      "file": "000065.sst",
      "reason": "Invalid checksum",
      "records_affected": 15000
    }
  ],
  "warnings": [
    "High record skip count detected: 22000 records skipped"
  ],
  "backup": {
    "created": true,
    "path": "/tmp/replica_backup/4.27",
    "cleaned_on_success": true
  }
}
```

## 7. Testing Strategy

### 7.1 Unit Tests

- Metadata loading and generation
- SST file repair
- Error handling and recovery strategies
- Boundary cases and threshold checking
- Backup and rollback mechanisms
- Verification logic

### 7.2 Integration Tests

- End-to-end repair workflow
- Various corruption scenarios:
  - MANIFEST file completely missing
  - Partial SST file corruption (10%, 50%, 90%)
  - Corrupted metadata files only
  - Mixed corruption types
- Performance testing
- Backup and rollback testing
- Verification testing

### 7.3 Acceptance Criteria

**Functional**:
- [ ] Can repair MANIFEST-corrupted replicas
- [ ] Can handle SST file corruption
- [ ] Can generate correct metadata files
- [ ] Repaired replica can load successfully
- [ ] Data integrity verification passes
- [ ] Backup creation and rollback works correctly
- [ ] Verification catches repair failures
- [ ] Error thresholds are enforced correctly

**Performance**:
- [ ] Small replica (< 1GB): < 5 minutes
- [ ] Medium replica (1-10GB): < 30 minutes
- [ ] Large replica (> 10GB): < 2 hours

**Stability**:
- [ ] Tool doesn't worsen replica state
- [ ] Error handling degrades gracefully
- [ ] Memory usage reasonable (< 4GB)

## 8. Implementation Phases

### Phase 1: Core Functionality (2-3 days)
- Basic framework setup and shell command integration
- Parameter parsing and validation
- Backup creation and rollback mechanism
- Metadata file reading
- SST file scanning and copying
- Metadata file generation

### Phase 2: Enhanced Features (2-3 days)
- Concrete corruption type detection
- Enhanced error handling and recovery strategies
- Error threshold enforcement
- Detailed progress output
- JSON report generation
- Verification module

### Phase 3: Optimization and Testing (1-2 days)
- Concurrent processing
- Disk space checking
- Comprehensive testing
- Performance optimization

**Total Estimated Time**: 5-8 days

## 9. Usage Examples

### Example 1: Basic Repair
```bash
./run.sh shell
> use temp
> repair_replica 4.27 /path/to/corrupted/replica /path/to/repaired --report_file report.json
```

### Example 2: Diagnose Only
```bash
./run.sh shell
> repair_replica 4.27 /path/to/corrupted/replica /path/to/output --dry_run
```

### Example 3: Custom Thresholds
```bash
./run.sh shell
> repair_replica 4.27 /path/to/corrupted/replica /path/to/output \
    --max_corrupted_ratio 0.7 --skip_corrupted_records
```

### Example 4: With Custom Backup Location
```bash
./run.sh shell
> repair_replica 4.27 /path/to/corrupted/replica /path/to/output \
    --backup_dir /mnt/backup/replica_4.27
```

## 10. Design Revision History

**v1.0 (2026-06-08)**: Initial design
- SST file scanning and reconstruction approach
- Basic error handling
- JSON reporting

**v1.1 (2026-06-08)**: Enhanced design based on review feedback
- Changed from standalone executable to shell command integration
- Added backup and rollback mechanism
- Added post-repair verification
- Enhanced error handling with concrete recovery strategies
- Added corruption type detection
- Improved error threshold enforcement
- Enhanced testing scenarios
- Added Pegasus key schema validation details

## 11. References

- `local_partition_split` tool implementation (src/shell/commands/local_partition_split.cpp)
- RocksDB SST File API documentation
- Pegasus metadata structures (src/replica/replication_app_base.h)
- Pegasus error codes (src/utils/error_code.h)
- Pegasus key schema (base/pegasus_key_schema.h)
