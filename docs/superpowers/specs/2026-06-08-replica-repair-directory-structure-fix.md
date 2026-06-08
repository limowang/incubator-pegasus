# Replica Repair Tool - Directory Structure Fix Design

## Date
2026-06-08

## Problem Statement

The replica repair tool currently assumes the following directory structure:
```
replica_dir/
└── rdb/          <-- RocksDB files
```

However, the actual Pegasus replica directory structure is:
```
replica_dir/
├── data/
│   └── rdb/      <-- Actual RocksDB files location
├── .init-info
└── .app-info
```

This mismatch causes the repair tool to fail because it cannot find the rdb directory.

## Solution Design

### Approach: Direct Path Fix

Modify all path construction in `replica_repair.cpp` to use the correct directory structure by changing:
```cpp
path_combine(dir, "rdb")
```
to:
```cpp
path_combine(dir, "data/rdb")
```

### Affected Code Locations

The modification impacts 7 locations in `src/shell/commands/replica_repair.cpp`:

| Line | Function | Purpose |
|------|----------|---------|
| 214-217 | `validate_config()` | Validate replica directory structure |
| 279-282 | `create_backup()` | Verify backup directory |
| 374-377 | `discover_sst_files()` | Find SST files for repair |
| 627-630 | `create_repaired_database()` | Create new RocksDB database |
| 664-667 | `import_repaired_sst_files()` | Import repaired SST files |
| 882-885 | `repair_replica()` | Read RocksDB metadata |
| 1029 | `verify_repair()` | Verify repaired database |

### Output Directory Structure

The repaired output directory will follow the same structure:
```
output_dir/
├── data/
│   └── rdb/          <-- Repaired RocksDB files
├── .init-info        <-- Copied from original replica
└── .app-info         <-- Copied from original replica
```

### Backup Directory Structure

Backup uses `cp -r` to copy the entire replica directory, preserving the structure:
```
backup_dir/
├── data/
│   └── rdb/
├── .init-info
└── .app-info
```

## Implementation Details

### Code Changes

For each of the 7 identified locations, replace:
```cpp
auto rdb_dir = dsn::utils::filesystem::path_combine(
    base_dir,
    "rdb"
);
```

With:
```cpp
auto rdb_dir = dsn::utils::filesystem::path_combine(
    base_dir,
    "data/rdb"
);
```

Where `base_dir` is one of:
- `config.replica_dir` (source replica directory)
- `backup_dir` (backup directory)
- `output_dir` (target repaired directory)

### Directory Creation

The `create_repaired_database()` function will need to ensure the `data` subdirectory exists before creating the `rdb` directory:
```cpp
auto data_dir = dsn::utils::filesystem::path_combine(output_dir, "data");
if (!dsn::utils::filesystem::directory_exists(data_dir)) {
    if (!dsn::utils::filesystem::create_directory(data_dir)) {
        // Handle error
    }
}
```

## Testing Considerations

### Unit Tests
- Verify directory structure validation works with `data/rdb` path
- Test backup creation and verification
- Test SST file discovery from correct path

### Integration Tests
- Test full repair workflow with actual replica directory structure
- Verify output directory structure matches expected format
- Test metadata file copying (`.init-info`, `.app-info`)

### Test Script Updates

The test script `src/shell/commands/test_replica_repair.sh` should create test directories with the correct structure:
```bash
mkdir -p test_replica/data/rdb
touch test_replica/.init-info
touch test_replica/.app-info
```

## Validation

After implementation, validate:
1. Repair tool can correctly identify rdb directory
2. Backup operations preserve directory structure
3. Repaired output follows the correct structure
4. All metadata files are properly copied

## Risk Assessment

### Low Risk
- Changes are straightforward path string modifications
- No logic changes
- Backup operations remain safe
- Rollback is simple (revert path changes)

### Mitigation
- Test with actual replica directories before deployment
- Verify backup creation works correctly
- Ensure metadata file paths remain unchanged

## Success Criteria

- Repair tool successfully processes replicas with `data/rdb` structure
- Output directories follow the same structure
- All tests pass
- No regression in existing functionality

## Related Documentation

- Main repair tool documentation: `docs/replica_repair.md`
- Original design spec: `docs/superpowers/specs/2026-06-08-replica-repair-tool-design.md`
