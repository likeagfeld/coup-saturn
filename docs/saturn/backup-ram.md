# Saturn Backup RAM

The Saturn has 32KB of internal backup RAM for save data, accessed through
the BUP (Backup) BIOS API. This document covers the on-disk format used by
Mednafen and the `tools/saturn_save_reader.py` tool for reading save files
on the host.

## Mednafen .bkr Files

Mednafen stores Saturn backup RAM as `.bkr` files:

```
~/.mednafen/sav/game.<md5>.bkr
```

The `<md5>` is the MD5 hash of the disc image (ISO/CUE). Each rebuild of
your program produces a new ISO with a new hash, so **saves don't persist
across rebuilds**. Workarounds:

- Copy the `.bkr` file between hashes: `cp game.<old>.bkr game.<new>.bkr`
- Use `--all-files` with the save reader to check all `.bkr` files at once
- Use glob patterns: `game.*.bkr`

## BUP Filesystem Format

A `.bkr` file is exactly 32768 bytes = 512 blocks of 64 bytes each.

| Block | Purpose | Description |
|-------|---------|-------------|
| 0 | Signature | `"BackUpRam Format"` repeated to fill 64 bytes |
| 1 | Management | Free block tracking (zeros when empty) |
| 2+ | Directory/Data | Directory entries or file data blocks |

### Directory Entries

A directory entry occupies one 64-byte block. Byte 0 has the `0x80` flag
set to indicate a valid entry.

```
Offset  Size  Field
------  ----  -----
+00     4     Flags/status (byte 0 = 0x80 = valid)
+04     11    Filename (space-padded, null-terminated)
+0F     1     Null terminator
+10     10    Comment (space-padded)
+1A     1     Language code
+1B     4     Packed date (big-endian, BUP_SetDate format)
+1F     1     Padding
+20     2     Data size in bytes (uint16, big-endian)
+22     30    Block chain (up to 15 uint16 big-endian block numbers)
```

The block chain lists the blocks allocated to the file. For files needing
more than 15 blocks, the first entries in the chain point to **chain
continuation blocks** whose data portions hold additional 2-byte block
refs (up to 30 per block). The last continuation block may be a hybrid:
chain refs (terminated by `0x0000`) followed by the start of the file data.

### Chain Continuation Blocks

When a file needs more blocks than fit in the 15-slot directory chain, the
first N blocks in the chain are continuation blocks:

```
+00  4 bytes  Header (0x00000000)
+04  60 bytes Up to 30 uint16 big-endian block references (0x0000 = end)
```

After the chain refs end (at the `0x0000` terminator), the remaining bytes
in the last continuation block are the start of the file data.

### Data Blocks

Each data block is 64 bytes with a 4-byte header (all zeros) followed by
60 bytes of usable data:

```
+00  4 bytes  Header (0x00000000)
+04  60 bytes File data
```

To reconstruct a file:
1. Collect the full block list from the directory chain + continuation blocks
2. Read any partial file data from the end of the last continuation block
3. Read full data blocks (skip 4-byte header, take 60 bytes each)
4. Concatenate and trim to the declared data size

### Packed Date Format

The 4-byte date field stores (from `saturn_bup_date_t`):

| Byte | Field | Range |
|------|-------|-------|
| 0 | Year | Offset from 1980 |
| 1 | Month | 1-12 |
| 2 | Day | 1-31 |
| 3 | Hour | 0-23 |

## Save Reader Tool

`tools/saturn_save_reader.py` reads `.bkr` files on the host:

```bash
# Filesystem summary
python3 tools/saturn_save_reader.py file.bkr --info

# List all saved files
python3 tools/saturn_save_reader.py file.bkr --list

# Extract raw data to a file
python3 tools/saturn_save_reader.py file.bkr --extract NLTEST_LOG -o out.bin

# Hex dump a file's contents
python3 tools/saturn_save_reader.py file.bkr --hexdump NLTEST_LOG

# Parse with auto-detected format
python3 tools/saturn_save_reader.py file.bkr --parse NLTEST_LOG

# Parse with explicit format
python3 tools/saturn_save_reader.py file.bkr --parse NLTEST_LOG --format netlink

# List + parse all files
python3 tools/saturn_save_reader.py file.bkr --all

# Glob multiple .bkr files (newest first, only process first)
python3 tools/saturn_save_reader.py ~/.mednafen/sav/game.*.bkr --list

# Process all .bkr files
python3 tools/saturn_save_reader.py ~/.mednafen/sav/game.*.bkr --list --all-files
```

When given multiple `.bkr` files, the tool sorts by modification time
(newest first) and only processes the first unless `--all-files` is passed.

## Adding New Format Parsers

To support a new save format:

1. Define magic bytes and constants at the top of `tools/saturn_save_reader.py`
2. Write a parser function: `def parse_my_format(data: bytes) -> dict`
3. Write a display function: `def display_my_format_parsed(parsed: dict)`
4. Register in `FORMAT_PARSERS` and `DISPLAY_FUNCS` dicts
5. Add magic byte detection to `detect_format()`

Example:

```python
MY_MAGIC = 0x4D594D47  # "MYMG"

def parse_my_format(data: bytes) -> dict:
    magic = struct.unpack('>I', data[:4])[0]
    if magic != MY_MAGIC:
        return {"error": f"Invalid magic: 0x{magic:08X}"}
    # ... parse fields ...
    return {"magic": f"0x{magic:08X}", ...}

def display_my_format_parsed(parsed: dict):
    if "error" in parsed:
        print(f"  Parse error: {parsed['error']}")
        return
    # ... pretty print ...

FORMAT_PARSERS["myformat"] = parse_my_format
DISPLAY_FUNCS["myformat"] = display_my_format_parsed
```

Then add detection in `detect_format()`:

```python
if magic == MY_MAGIC:
    return "myformat"
```

## Satiator Vmem Exports

The Satiator ODE can export individual save files as `.BUP` files using the
"Vmem" format. Unlike `.bkr` files (which are full 32KB RAM dumps), Vmem
exports contain a single file with a 64-byte header followed by raw data.

### Vmem Header Format

```
Offset  Size  Field
------  ----  -----
+00     4     Magic ("Vmem" = 0x566D656D)
+04     11    Filename (null-terminated)
+0F     1     Padding
+10     10    Comment (null-terminated)
+1A     1     Language code
+1B     4     Packed date (BUP_SetDate format)
+1F     1     Padding
+20     4     Data size in bytes (uint32, big-endian)
+24     28    Reserved (zeros)
```

Total header: 64 bytes. File data follows immediately after the header with
no block structure — just raw payload bytes.

### Reading Vmem Files

`tools/saturn_save_reader.py` auto-detects the Vmem format by checking for
the "Vmem" magic at offset 0:

```bash
# List contents of a Satiator export
python3 tools/saturn_save_reader.py save.BUP --list

# Parse with auto-detected format
python3 tools/saturn_save_reader.py save.BUP --parse NLTEST_LOG

# Hex dump
python3 tools/saturn_save_reader.py save.BUP --hexdump NLTEST_LOG
```

### Key Differences from .bkr

| Aspect | .bkr (Mednafen) | .BUP (Satiator Vmem) |
|--------|-----------------|----------------------|
| Size | Fixed 32768 bytes | Variable (64 + data size) |
| Contents | Full RAM image (all saves) | Single file export |
| Structure | 512 blocks of 64 bytes | 64-byte header + raw data |
| Block chain | Yes (directory → data blocks) | No (contiguous data) |
| Source | Emulator save state | Real hardware via Satiator |

## C API

The Saturn storage C API is in `pal/saturn/saturn_storage.h`. Key types:

- `saturn_bup_dir_t` — directory entry (filename, comment, date, size)
- `saturn_bup_stat_t` — device status (total/free blocks)
- `saturn_bup_date_t` — unpacked date structure

The `cui_pal_storage_t` interface provides `save()`, `load()`, `exists()`,
`delete()`, and `free_space()` for platform-agnostic use.
