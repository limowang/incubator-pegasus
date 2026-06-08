# Replica Repair Tool

## Overview

The `repair_replica` command is a shell utility for repairing corrupted Pegasus replica databases by extracting valid data from damaged SST files and creating a new consistent database.

## Usage

```bash
repair_replica <gpid> <replica_dir> <output_dir> [options]
```

### Arguments

- `gpid`: Partition ID in format `app_id.partition_id` (e.g., `1.2`)
- `replica_dir`: Path to the corrupted replica directory
- `output_dir`: Path where the repaired replica will be created

### Options

- `--backup_dir <path>`: Custom backup directory path (auto-generated if not specified)
- `--no_backup`: Disable backup creation (NOT recommended)
- `--report_file <path>`: Generate JSON report file
- `--dry_run`: Analyze without performing actual repair
- `--skip_corrupted_records`: Skip corrupted records and continue
- `--max_corrupted_ratio <ratio>`: Maximum corrupted file ratio (0.0-1.0, default: 0.5)
- `--verify_repair`: Verify repaired replica after completion (default: true)

## Examples

### Basic Repair
```bash
repair_replica 1.2 /path/to/corrupted/replica /path/to/output
```

### Dry Run Analysis
```bash
repair_replica 1.2 /path/to/corrupted/replica /path/to/output --dry_run
```

### Custom Backup Location
```bash
repair_replica 1.2 /path/to/corrupted/replica /path/to/output --backup_dir /custom/backup
```

### Generate JSON Report
```bash
repair_replica 1.2 /path/to/corrupted/replica /path/to/output --report_file repair_report.json
```

### Skip Corrupted Data
```bash
repair_replica 1.2 /path/to/corrupted/replica /path/to/output --skip_corrupted_records --max_corrupted_ratio 0.3
```

## Workflow

1. **Validation**: Verify directory structure and permissions
2. **Backup**: Create backup of original replica (safety measure)
3. **Discovery**: Find all SST files in the replica
4. **Repair**: Process each SST file, extracting valid records
5. **Reconstruction**: Create new database and import repaired data
6. **Metadata**: Generate required metadata files
7. **Verification**: Verify database consistency (optional)
8. **Reporting**: Generate JSON report (optional)

## Directory Structure

The replica directory must follow this structure:

```
replica_dir/
├── data/
│   └── rdb/              # RocksDB database files (SST, CURRENT, MANIFEST, etc.)
├── .init-info            # Replica initialization metadata
└── .app-info             # Application metadata
```

The repaired output will follow the same structure:

```
output_dir/
├── data/
│   └── rdb/              # Repaired RocksDB database
├── .init-info            # Copied from original replica
└── .app-info             # Copied from original replica
```

## Safety Features

- **Automatic Backup**: Creates backup before any modifications
- **Rollback Support**: Can rollback to backup on failure
- **Dry Run Mode**: Test without making changes
- **Verification**: Optional database verification after repair

## JSON Report Format

```json
{
  "success": true,
  "verification_passed": true,
  "gpid": "1.2",
  "replica_dir": "/path/to/replica",
  "output_dir": "/path/to/output",
  "backup_path": "/tmp/replica_backup_1.2.1234567890",
  "statistics": {
    "total_sst_files": 10,
    "corrupted_sst_files": 2,
    "total_records": 1000000,
    "recovered_records": 950000,
    "skipped_records": 50000,
    "data_size_bytes": 1073741824,
    "duration_seconds": 45.67
  },
  "warnings": ["Some warning message"],
  "error_message": ""
}
```

## Testing

Run the test suite:

```bash
./src/shell/commands/test_replica_repair.sh
```

## Implementation Notes

- Uses RocksDB SST file reader/writer for data extraction
- Supports incremental repair with skip_corrupted_records option
- Preserves metadata (decree, data version) from original database
- Compatible with Pegasus column family structure

## Task Completion Status

- **Task 13**: Verification implemented ✓
- **Task 14**: JSON reporting implemented ✓
- **Task 15**: Basic test framework created ✓
- **Task 16**: Documentation added ✓
- **Task 17**: Code review complete ✓

## TODO

- Implement comprehensive integration tests with valid SST files
- Add parallel processing for large-scale repairs
- Implement progress callback for long-running operations
- Add support for incremental backups
- Enhance error reporting with recovery suggestions

## Limitations

- Requires valid SST file structure (must be readable by RocksDB)
- Large databases may require significant disk space for backup
- Repair time depends on data size and corruption level
- Does not handle structural metadata corruption (requires manual intervention)