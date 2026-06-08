# Pegasus Replica Repair Tool - Design Specification

**Date**: 2026-06-08
**Author**: Claude Code
**Status**: Draft
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

Create an independent C++ executable tool that:
- Uses `rocksdb::SstFileReader` to scan existing SST files
- Validates and recovers data from corrupted files
- Creates a new database instance with recovered data
- Generates required metadata files
- Provides detailed progress output and JSON reports

**Key Design Decision**: Single replica repair mode requiring explicit parameters (gpid, replica_dir, output_dir).

## 2. Architecture and Components

### 2.1 Tool Architecture

```
pegasus_replica_repair (standalone executable)
├── Command-line Interface
├── Diagnosis Module
│   ├── Corruption Type Detection
│   ├── SST File Scanner
│   └── Recoverability Assessment
├── Repair Engine
│   ├── SST Reader
│   ├── Data Validator
│   └── New Database Writer
├── Metadata Generator
│   ├── .app-info Generator
│   └── .init-info Generator
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

#### 2.2.2 Diagnosis Module
- Detect corruption types (MANIFEST corruption, key order issues, SST file corruption)
- Scan replica directory, collect all SST file information
- Assess recoverability, estimate recoverable data volume

#### 2.2.3 Repair Engine
- Use `rocksdb::SstFileReader` to read each SST file
- Validate each record (key format, data integrity)
- Create new database instance, write valid data
- Handle Pegasus-specific data format (hashkey + sortkey)

#### 2.2.4 Metadata Generator
- Generate `.app-info` file with application information
- Generate `.init-info` file with initialization state
- Handle both existing metadata (from original replica) and default values

#### 2.2.5 Report Generator
- Real-time progress output (scanned files, recovered records)
- Generate JSON format detailed report (before/after comparison, data statistics)

#### 2.2.6 Error Handler
- Log all skipped records with reasons
- Provide detailed error messages and recommendations

## 3. Data Structures

### 3.1 Command-line Interface

```bash
./pegasus_replica_repair \
    --gpid <gpid> \
    --replica_dir <path> \
    --output_dir <path> \
    [--report_file <path>] \
    [--log_level <info|debug|warning>] \
    [--dry_run] \
    [--skip_corrupted_records] \
    [--load_metadata] \
    [--help]
```

**Parameters**:
- `--gpid`: Global partition ID (format: `<app_id>.<partition_id>`, e.g., `4.27`)
- `--replica_dir`: Corrupted replica directory path
- `--output_dir`: New replica output directory path
- `--report_file`: JSON report file path (optional, default: stdout)
- `--log_level`: Log level (default: info)
- `--dry_run`: Diagnose only without actual repair
- `--skip_corrupted_records`: Skip corrupted records and continue repair
- `--load_metadata`: Load metadata from original replica if files are intact
- `--help`: Display help information

### 3.2 Core Data Structures

```cpp
// Repair configuration
struct RepairConfig {
    int32_t app_id;                    // From gpid
    int32_t partition_id;              // From gpid
    std::string replica_dir;           // Original replica directory
    std::string output_dir;            // Output directory
    std::string report_file;           // Report file path
    bool dry_run;                      // Diagnose only mode
    bool skip_corrupted;               // Skip corrupted records
    bool load_metadata;                // Load metadata from original
    dsn::logging::log_level log_level; // Log level
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
1. Parse and validate parameters
   ├── Parse gpid, replica_dir, output_dir
   └── Validate directory existence and permissions

2. Load metadata from original replica
   ├── Try to read .app-info file
   ├── Try to read .init-info file
   └── Use default values if files are corrupted

3. Open original RocksDB in read-only mode
   ├── Create column family handles (data and meta)
   └── Read metadata using meta_store

4. Scan all SST files
   ├── Get live files metadata
   ├── Skip meta column family files
   └── Collect file information

5. Create new RocksDB instance
   ├── Create new database directory
   ├── Open new database (create_if_missing = true)
   └── Create required column families

6. Repair SST files
   ├── For each SST file:
   │   ├── Open with SstFileReader
   │   ├── Verify checksum
   │   ├── Iterate through records
   │   ├── Validate and write to new SST file
   │   └── Skip corrupted records (if enabled)
   └── Close all writers

7. Import repaired SST files
   ├── Use IngestExternalFile
   └── Optional: full compaction

8. Set metadata to new database
   ├── Use meta_store to set metadata
   └── Flush to ensure persistence

9. Generate metadata files
   ├── Generate .app-info file
   └── Generate .init-info file

10. Generate repair report
    ├── Collect statistics
    └── Generate JSON report
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

### 5.2 Error Handling Strategy

**Checksum Failed**: Skip file, log warning (unless `--skip_corrupted=false`)
**Corrupted Data**: Try to skip corrupted records, continue processing
**File Not Found**: Warning and skip
**Permission Denied**: Fatal error, terminate repair
**Metadata File Missing**: Use default values + warning
**Metadata File Corrupted**: Try to salvage fields, use default values + warning

### 5.3 Boundary Cases

**Empty Replica**:
```cpp
if (files.empty() && !metadata_exists) {
    result.error_message = "Replica is empty, no data to recover";
    return false;
}
```

**Partial Corruption**:
```cpp
double corrupted_ratio = static_cast<double>(corrupted_files.size()) / total_files;
if (corrupted_ratio > 0.5) {
    result.warnings.push_back(
        fmt::format("More than 50% of files are corrupted ({}/{})",
                   corrupted_files.size(), total_files));
}
```

**Insufficient Disk Space**:
```cpp
int64_t required_space = calculate_required_space(original_replica_dir);
int64_t available_space = get_available_disk_space(output_dir);

if (available_space < required_space * 1.5) {
    result.error_message = "Insufficient disk space";
    return false;
}
```

## 6. Reporting

### 6.1 Progress Output

```cpp
class RepairProgress {
public:
    void on_start_file(const std::string& filename);
    void on_file_complete(const std::string& filename, int64_t records);
    void on_file_skipped(const std::string& filename, const std::string& reason);
    void on_progress_update(int64_t total_files, int64_t processed_files);
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
    "output_dir": "/path/to/repaired/replica"
  },
  "diagnosis": {
    "corruption_type": "MANIFEST_corrupted",
    "total_sst_files": 150,
    "corrupted_sst_files": 2,
    "estimated_recoverable_records": 1500000
  },
  "statistics": {
    "total_records": 1520000,
    "recovered_records": 1498000,
    "skipped_records": 22000,
    "data_size_mb": 850.5,
    "duration_seconds": 245.8
  },
  "skipped_files": [
    {
      "file": "000065.sst",
      "reason": "Invalid checksum"
    }
  ],
  "warnings": []
}
```

## 7. Testing Strategy

### 7.1 Unit Tests

- Metadata loading and generation
- SST file repair
- Error handling
- Boundary cases

### 7.2 Integration Tests

- End-to-end repair workflow
- Various corruption scenarios
- Performance testing

### 7.3 Acceptance Criteria

**Functional**:
- [ ] Can repair MANIFEST-corrupted replicas
- [ ] Can handle SST file corruption
- [ ] Can generate correct metadata files
- [ ] Repaired replica can load successfully
- [ ] Data integrity verification passes

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
- Basic framework setup
- Parameter parsing and validation
- Metadata file reading
- SST file scanning and copying
- Metadata file generation

### Phase 2: Enhanced Features (1-2 days)
- Error handling and skip mechanism
- Detailed progress output
- JSON report generation

### Phase 3: Optimization (1 day)
- Concurrent processing
- Disk space checking
- dry_run mode

**Total Estimated Time**: 4-6 days

## 9. Usage Examples

### Example 1: Basic Repair
```bash
./pegasus_replica_repair \
    --gpid 4.27 \
    --replica_dir /sensorsdata/rnddata01/skv_offline/replica/reps/4.27.pegasus \
    --output_dir /tmp/replica_repair/4.27.repaired \
    --report_file /tmp/repair_report_4.27.json
```

### Example 2: Diagnose Only
```bash
./pegasus_replica_repair \
    --gpid 4.27 \
    --replica_dir /sensorsdata/rnddata01/skv_offline/replica/reps/4.27.pegasus \
    --output_dir /tmp/replica_repair/4.27.repaired \
    --dry_run
```

### Example 3: Skip Corrupted Records
```bash
./pegasus_replica_repair \
    --gpid 4.27 \
    --replica_dir /sensorsdata/rnddata01/skv_offline/replica/reps/4.27.pegasus \
    --output_dir /tmp/replica_repair/4.27.repaired \
    --skip_corrupted_records \
    --log_level debug
```

## 10. References

- `local_partition_split` tool implementation (src/shell/commands/local_partition_split.cpp)
- RocksDB SST File API documentation
- Pegasus metadata structures (src/replica/replication_app_base.h)
- Pegasus error codes (src/utils/error_code.h)
