#!/usr/bin/env python3
"""
Saturn Backup RAM Save File Reader

Universal tool for reading Mednafen .bkr files (Saturn backup RAM images).
Parses the BUP filesystem structure, lists files, extracts data, and can
parse known save formats (e.g., netlink test logs).

BUP filesystem layout (32768 bytes = 512 blocks of 64 bytes):
  Block 0:   Signature ("BackUpRam Format" repeated)
  Block 1:   Management / free block tracking
  Block 2+:  Directory entries (0x80 flag) or data blocks (4-byte header)

Usage:
    # Show filesystem info
    saturn_save_reader.py file.bkr --info

    # List all saved files
    saturn_save_reader.py file.bkr --list

    # Extract a file's raw data
    saturn_save_reader.py file.bkr --extract NLTEST_LOG --output out.bin

    # Hex dump a file
    saturn_save_reader.py file.bkr --hexdump NLTEST_LOG

    # Parse with auto-detected format
    saturn_save_reader.py file.bkr --parse NLTEST_LOG

    # Process newest of multiple .bkr files
    saturn_save_reader.py ~/.mednafen/sav/game.*.bkr --list

See docs/saturn/backup-ram.md for format details.
"""

import sys
import os
import struct
import argparse
from pathlib import Path
from typing import Optional

# ============================================================================
# BUP Constants
# ============================================================================

BUP_BLOCK_SIZE = 64
BUP_FILE_SIZE = 32768
BUP_BLOCK_COUNT = BUP_FILE_SIZE // BUP_BLOCK_SIZE
BUP_SIGNATURE = b"BackUpRam Format"
BUP_DIR_FLAG = 0x80
BUP_DATA_HEADER_SIZE = 4
BUP_DATA_PER_BLOCK = BUP_BLOCK_SIZE - BUP_DATA_HEADER_SIZE  # 60 bytes

# Satiator "Vmem" single-file export format
VMEM_MAGIC = b"Vmem"
VMEM_HEADER_SIZE = 64
VMEM_FILENAME_OFFSET = 0x10
VMEM_COMMENT_OFFSET = 0x1C
VMEM_LANGUAGE_OFFSET = 0x28
VMEM_DATE_OFFSET = 0x29
VMEM_DATASIZE_OFFSET = 0x2E
VMEM_DATA_OFFSET = 0x40

# Directory entry offsets within a 64-byte block
DIR_FLAGS_OFFSET = 0
DIR_FILENAME_OFFSET = 4
DIR_FILENAME_LEN = 11
DIR_COMMENT_OFFSET = 0x10
DIR_COMMENT_LEN = 10
DIR_LANGUAGE_OFFSET = 0x1A
DIR_DATE_OFFSET = 0x1B
DIR_DATASIZE_OFFSET = 0x20
DIR_CHAIN_OFFSET = 0x22
DIR_MAX_CHAIN_ENTRIES = 15  # (64 - 0x22) / 2 = 15

# ============================================================================
# Netlink Save Format Constants
# ============================================================================

NETLINK_MAGIC = 0x4E4C5453  # "NLTS"
CHATLOG_MAGIC = 0x434C4F47  # "CLOG"
CHATLOG_MAX_LOGS = 64
CHATLOG_LOG_LINE_SIZE = 48
NETLINK_VERSION = 1
NETLINK_MAX_LOGS = 64
NETLINK_LOG_LINE_SIZE = 48

TEST_STATUS = {
    0: "PENDING",
    1: "PASS",
    2: "FAIL",
    3: "SKIP",
}

# ============================================================================
# BUP Directory Entry
# ============================================================================


class BupDirEntry:
    """Parsed BUP directory entry."""

    def __init__(self, block_num: int, raw_block: bytes):
        self.block_num = block_num
        self.flags = raw_block[DIR_FLAGS_OFFSET]

        # Filename: 11 bytes, space-padded, null-terminated
        name_raw = raw_block[DIR_FILENAME_OFFSET:DIR_FILENAME_OFFSET + DIR_FILENAME_LEN]
        null_pos = name_raw.find(b'\x00')
        if null_pos != -1:
            name_raw = name_raw[:null_pos]
        self.filename = name_raw.decode('ascii', errors='replace').rstrip()

        # Comment: 10 bytes, space-padded
        comment_raw = raw_block[DIR_COMMENT_OFFSET:DIR_COMMENT_OFFSET + DIR_COMMENT_LEN]
        null_pos = comment_raw.find(b'\x00')
        if null_pos != -1:
            comment_raw = comment_raw[:null_pos]
        self.comment = comment_raw.decode('ascii', errors='replace').rstrip()

        # Language code
        self.language = raw_block[DIR_LANGUAGE_OFFSET]

        # Packed date (4 bytes big-endian)
        self.date_packed = struct.unpack_from('>I', raw_block, DIR_DATE_OFFSET)[0]

        # Data size (2 bytes big-endian at offset 0x20)
        self.datasize = struct.unpack_from('>H', raw_block, DIR_DATASIZE_OFFSET)[0]

        # Block chain (2-byte block numbers)
        self.chain = []
        for i in range(DIR_MAX_CHAIN_ENTRIES):
            offset = DIR_CHAIN_OFFSET + (i * 2)
            if offset + 2 > len(raw_block):
                break
            block_ref = struct.unpack_from('>H', raw_block, offset)[0]
            if block_ref == 0:
                break
            self.chain.append(block_ref)

    @property
    def is_valid(self) -> bool:
        return (self.flags & BUP_DIR_FLAG) != 0

    def format_date(self) -> str:
        """Decode BUP packed date to human-readable string.

        The BUP BIOS BUP_SetDate() packs saturn_bup_date_t into 32 bits.
        The exact bit layout is undocumented; we try byte-packed first
        (year|month|day|hour), then fall back to showing raw hex.

        Note: emulated SMPC RTC values may produce odd dates.
        """
        if self.date_packed == 0:
            return "(no date)"
        b = self.date_packed.to_bytes(4, 'big')
        year = 1980 + b[0]
        month = b[1]
        day = b[2]
        hour = b[3]
        if 1 <= month <= 12 and 1 <= day <= 31 and hour <= 23:
            return f"{year:04d}-{month:02d}-{day:02d} {hour:02d}:00"
        return f"0x{self.date_packed:08X}"


class VmemFile:
    """Parsed Satiator "Vmem" single-file BUP export."""

    def __init__(self, data: bytes):
        self.block_num = 0
        self.flags = 0x80

        name_raw = data[VMEM_FILENAME_OFFSET:VMEM_FILENAME_OFFSET + 11]
        null_pos = name_raw.find(b'\x00')
        if null_pos != -1:
            name_raw = name_raw[:null_pos]
        self.filename = name_raw.decode('ascii', errors='replace').rstrip()

        comment_raw = data[VMEM_COMMENT_OFFSET:VMEM_COMMENT_OFFSET + 10]
        null_pos = comment_raw.find(b'\x00')
        if null_pos != -1:
            comment_raw = comment_raw[:null_pos]
        self.comment = comment_raw.decode('ascii', errors='replace').rstrip()

        self.language = data[VMEM_LANGUAGE_OFFSET]
        self.date_packed = struct.unpack_from('>I', data, VMEM_DATE_OFFSET)[0]
        self.datasize = struct.unpack_from('>H', data, VMEM_DATASIZE_OFFSET)[0]
        self.chain = []  # not applicable for Vmem files
        self._file_data = data[VMEM_DATA_OFFSET:VMEM_DATA_OFFSET + self.datasize]

    @property
    def is_valid(self) -> bool:
        return len(self._file_data) > 0

    def format_date(self) -> str:
        if self.date_packed == 0:
            return "(no date)"
        b = self.date_packed.to_bytes(4, 'big')
        year = 1980 + b[0]
        month = b[1]
        day = b[2]
        hour = b[3]
        if 1 <= month <= 12 and 1 <= day <= 31 and hour <= 23:
            return f"{year:04d}-{month:02d}-{day:02d} {hour:02d}:00"
        return f"0x{self.date_packed:08X}"


def is_vmem_file(data: bytes) -> bool:
    """Check if data starts with the Satiator 'Vmem' magic."""
    return len(data) >= VMEM_HEADER_SIZE and data[:4] == VMEM_MAGIC


# ============================================================================
# BUP Filesystem Parser
# ============================================================================


def parse_bup_header(data: bytes) -> bool:
    """Verify BUP signature in block 0."""
    if len(data) < BUP_BLOCK_SIZE:
        return False
    return data[:len(BUP_SIGNATURE)] == BUP_SIGNATURE


def scan_directory(data: bytes) -> list[BupDirEntry]:
    """Scan blocks 2+ for directory entries (0x80 flag)."""
    entries = []
    for block_num in range(2, BUP_BLOCK_COUNT):
        block_start = block_num * BUP_BLOCK_SIZE
        block_end = block_start + BUP_BLOCK_SIZE
        if block_end > len(data):
            break
        block_data = data[block_start:block_end]
        if block_data[0] & BUP_DIR_FLAG:
            entry = BupDirEntry(block_num, block_data)
            if entry.is_valid and entry.filename:
                entries.append(entry)
    return entries


def extract_file_data(data: bytes, entry: BupDirEntry) -> bytes:
    """Follow block chain, strip 4-byte headers, return raw file data.

    BUP chain layout for files needing more than 15 blocks:
    - The directory entry holds up to 15 block refs in its chain area.
    - When more blocks are needed, the first N blocks in the chain are
      chain continuation blocks: their 60-byte data portions hold
      2-byte big-endian block refs (up to 30 per block).
    - The last continuation block may be a hybrid: chain refs followed
      by the start of the file data (after a 0x0000 terminator).
    - Pure data blocks have a 4-byte zero header + 60 bytes of data.

    The file's raw data is: partial data from the last continuation block
    (after the chain refs end) + full data from all subsequent data blocks.
    """
    if not entry.chain:
        return b''

    # Step 1: Resolve the full block list by following chain continuations
    all_refs = list(entry.chain)
    continuation_count = 0
    last_cont_data_offset = 0  # byte offset where data starts in last continuation block

    while continuation_count < len(all_refs):
        block_num = all_refs[continuation_count]
        if block_num == 0 or block_num >= BUP_BLOCK_COUNT:
            break

        block_start = block_num * BUP_BLOCK_SIZE
        if block_start + BUP_BLOCK_SIZE > len(data):
            break

        block_data = data[block_start + BUP_DATA_HEADER_SIZE:block_start + BUP_BLOCK_SIZE]

        # Check if this block is a chain continuation:
        # First 2-byte ref must be a valid block number beyond current max
        if len(block_data) < 2:
            break

        first_ref = struct.unpack_from('>H', block_data, 0)[0]
        max_ref = max(all_refs)

        if first_ref == 0 or first_ref >= BUP_BLOCK_COUNT or first_ref <= max_ref:
            break

        # Parse chain refs from this continuation block
        new_refs = []
        byte_offset = 0
        for i in range(0, BUP_DATA_PER_BLOCK, 2):
            if i + 2 > len(block_data):
                break
            ref = struct.unpack_from('>H', block_data, i)[0]
            if ref == 0:
                byte_offset = i + 2  # data starts after the 0x0000 terminator
                break
            if ref >= BUP_BLOCK_COUNT:
                byte_offset = i
                break
            new_refs.append(ref)
            byte_offset = i + 2

        if not new_refs:
            break

        all_refs.extend(new_refs)
        last_cont_data_offset = byte_offset
        continuation_count += 1

    # Step 2: Build file data
    result = bytearray()

    # If there were continuation blocks, the last one may have partial file
    # data after the chain refs
    if continuation_count > 0 and last_cont_data_offset < BUP_DATA_PER_BLOCK:
        last_cont_block = all_refs[continuation_count - 1]
        block_start = last_cont_block * BUP_BLOCK_SIZE
        block_data = data[block_start + BUP_DATA_HEADER_SIZE:block_start + BUP_BLOCK_SIZE]
        result.extend(block_data[last_cont_data_offset:])

    # Read all data blocks (those after the continuation blocks)
    data_blocks = all_refs[continuation_count:]
    for block_ref in data_blocks:
        if block_ref == 0 or block_ref >= BUP_BLOCK_COUNT:
            break
        block_start = block_ref * BUP_BLOCK_SIZE
        if block_start + BUP_BLOCK_SIZE > len(data):
            break
        result.extend(data[block_start + BUP_DATA_HEADER_SIZE:block_start + BUP_BLOCK_SIZE])

    # Trim to declared data size
    if entry.datasize > 0 and len(result) > entry.datasize:
        result = result[:entry.datasize]

    return bytes(result)


def find_entry_by_name(entries: list[BupDirEntry], filename: str) -> Optional[BupDirEntry]:
    """Find a directory entry by filename (case-insensitive)."""
    filename_upper = filename.upper().strip()
    for entry in entries:
        if entry.filename.upper().strip() == filename_upper:
            return entry
    return None


# ============================================================================
# Format Parsers
# ============================================================================


def detect_format(data: bytes) -> Optional[str]:
    """Detect save format from magic bytes."""
    if len(data) >= 4:
        magic = struct.unpack('>I', data[:4])[0]
        if magic == NETLINK_MAGIC:
            return "netlink"
        if magic == CHATLOG_MAGIC:
            return "chatlog"
    return None


def parse_netlink_save(data: bytes) -> dict:
    """Parse netlink_save_t structure from extracted data."""
    if len(data) < 32:
        return {"error": "Data too short for netlink_save_t header"}

    magic, version, test_count, pad1, pad2 = struct.unpack('>IBBBB', data[0:8])

    if magic != NETLINK_MAGIC:
        return {"error": f"Invalid magic: 0x{magic:08X} (expected 0x{NETLINK_MAGIC:08X})"}

    results = []
    for i in range(min(test_count, 6)):
        offset = 8 + (i * 2)
        status = data[offset]
        results.append({
            "status": TEST_STATUS.get(status, f"UNKNOWN({status})"),
            "status_code": status,
        })

    uart_base, uart_stride, baud_idx = struct.unpack('>IHh', data[20:28])
    log_count = data[28]

    logs = []
    log_start = 32
    for i in range(min(log_count, NETLINK_MAX_LOGS)):
        line_offset = log_start + (i * NETLINK_LOG_LINE_SIZE)
        if line_offset + NETLINK_LOG_LINE_SIZE > len(data):
            break
        line_bytes = data[line_offset:line_offset + NETLINK_LOG_LINE_SIZE]
        null_pos = line_bytes.find(b'\x00')
        if null_pos != -1:
            line_bytes = line_bytes[:null_pos]
        logs.append(line_bytes.decode('ascii', errors='replace'))

    return {
        "magic": f"0x{magic:08X}",
        "version": version,
        "test_count": test_count,
        "results": results,
        "uart_base": f"0x{uart_base:08X}",
        "uart_stride": uart_stride,
        "baud_idx": baud_idx,
        "log_count": log_count,
        "logs": logs,
    }


def parse_chatlog_save(data: bytes) -> dict:
    """Parse chat_save_t structure from extracted data."""
    if len(data) < 8:
        return {"error": "Data too short for chat_save_t header"}

    magic, log_count, state, online, pad = struct.unpack('>IBBBB', data[0:8])

    if magic != CHATLOG_MAGIC:
        return {"error": f"Invalid magic: 0x{magic:08X} (expected 0x{CHATLOG_MAGIC:08X})"}

    STATE_NAMES = {
        0: "CONNECTING", 1: "AUTH_WAIT", 2: "NAME_ENTRY",
        3: "NAME_WAIT", 4: "CONNECTED", 5: "DISCONNECTED", 6: "ERROR",
    }

    logs = []
    log_start = 8
    for i in range(min(log_count, CHATLOG_MAX_LOGS)):
        line_offset = log_start + (i * CHATLOG_LOG_LINE_SIZE)
        if line_offset + CHATLOG_LOG_LINE_SIZE > len(data):
            break
        line_bytes = data[line_offset:line_offset + CHATLOG_LOG_LINE_SIZE]
        null_pos = line_bytes.find(b'\x00')
        if null_pos != -1:
            line_bytes = line_bytes[:null_pos]
        logs.append(line_bytes.decode('ascii', errors='replace'))

    return {
        "magic": f"0x{magic:08X}",
        "log_count": log_count,
        "state": STATE_NAMES.get(state, f"UNKNOWN({state})"),
        "state_code": state,
        "online": bool(online),
        "logs": logs,
    }


FORMAT_PARSERS = {
    "netlink": parse_netlink_save,
    "chatlog": parse_chatlog_save,
}


# ============================================================================
# Display Helpers
# ============================================================================


def hexdump(data: bytes, offset: int = 0):
    """Print hex dump with ASCII sidebar."""
    for i in range(0, len(data), 16):
        addr = offset + i
        chunk = data[i:i + 16]
        hex_part = ' '.join(f'{b:02X}' for b in chunk)
        ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"  {addr:04X}: {hex_part:<48s}  {ascii_part}")


def display_netlink_parsed(parsed: dict):
    """Pretty-print parsed netlink save data."""
    if "error" in parsed:
        print(f"  Parse error: {parsed['error']}", file=sys.stderr)
        return

    print(f"  Format:      netlink_save_t (NLTS)")
    print(f"  Version:     {parsed['version']}")
    print(f"  Test Count:  {parsed['test_count']}")
    print()
    print("  Test Results:")
    for i, result in enumerate(parsed['results']):
        print(f"    Test {i}: {result['status']}")
    print()
    print(f"  UART Base:   {parsed['uart_base']}")
    print(f"  UART Stride: {parsed['uart_stride']}")
    print(f"  Baud Index:  {parsed['baud_idx']}")
    print()
    print(f"  Log Lines:   {parsed['log_count']}")
    if parsed['logs']:
        print()
        for i, line in enumerate(parsed['logs']):
            print(f"    {i:2d}: {line}")


def display_chatlog_parsed(parsed: dict):
    """Pretty-print parsed chat log save data."""
    if "error" in parsed:
        print(f"  Parse error: {parsed['error']}", file=sys.stderr)
        return

    print(f"  Format:      chat_save_t (CLOG)")
    print(f"  State:       {parsed['state']}")
    print(f"  Online:      {parsed['online']}")
    print(f"  Log Lines:   {parsed['log_count']}")
    if parsed['logs']:
        print()
        for i, line in enumerate(parsed['logs']):
            print(f"    {i:2d}: {line}")


DISPLAY_FUNCS = {
    "netlink": display_netlink_parsed,
    "chatlog": display_chatlog_parsed,
}


# ============================================================================
# Commands
# ============================================================================


def cmd_info(data: bytes, path: Path):
    """Show BUP filesystem summary."""
    print(f"File:       {path}")
    print(f"Size:       {len(data)} bytes ({len(data) // BUP_BLOCK_SIZE} blocks)")

    if parse_bup_header(data):
        print(f"Signature:  Valid (\"BackUpRam Format\")")
    else:
        print(f"Signature:  INVALID (not a BUP image)")
        return

    entries = scan_directory(data)
    print(f"Files:      {len(entries)}")

    # Count used blocks (directory + data blocks referenced in chains)
    used_blocks = 2  # signature + management
    for entry in entries:
        used_blocks += 1  # directory entry itself
        used_blocks += len(entry.chain)
    free_blocks = BUP_BLOCK_COUNT - used_blocks
    print(f"Used:       {used_blocks} blocks ({used_blocks * BUP_BLOCK_SIZE} bytes)")
    print(f"Free:       ~{free_blocks} blocks (~{free_blocks * BUP_DATA_PER_BLOCK} bytes usable)")


def cmd_list(data: bytes, path: Path):
    """List all files in backup RAM."""
    if not parse_bup_header(data):
        print(f"Error: {path} is not a valid BUP image", file=sys.stderr)
        return False

    entries = scan_directory(data)
    if not entries:
        print("  (no files)")
        return True

    # Header
    print(f"  {'Filename':<12s} {'Size':>6s} {'Blocks':>6s}  {'Date':<18s} {'Comment'}")
    print(f"  {'-'*12} {'-'*6} {'-'*6}  {'-'*18} {'-'*10}")

    for entry in entries:
        date_str = entry.format_date()
        print(f"  {entry.filename:<12s} {entry.datasize:>6d} {len(entry.chain):>6d}  {date_str:<18s} {entry.comment}")

    return True


def cmd_extract(data: bytes, entries: list[BupDirEntry], filename: str, output: Optional[str]):
    """Extract a file's raw data."""
    entry = find_entry_by_name(entries, filename)
    if entry is None:
        print(f"Error: File '{filename}' not found in BUP image", file=sys.stderr)
        print(f"Available files: {', '.join(e.filename for e in entries)}", file=sys.stderr)
        return False

    file_data = extract_file_data(data, entry)

    if output:
        out_path = Path(output)
        out_path.write_bytes(file_data)
        print(f"Extracted {len(file_data)} bytes to {out_path}")
    else:
        sys.stdout.buffer.write(file_data)

    return True


def cmd_hexdump(data: bytes, entries: list[BupDirEntry], filename: str):
    """Hex dump a file's data."""
    entry = find_entry_by_name(entries, filename)
    if entry is None:
        print(f"Error: File '{filename}' not found in BUP image", file=sys.stderr)
        print(f"Available files: {', '.join(e.filename for e in entries)}", file=sys.stderr)
        return False

    file_data = extract_file_data(data, entry)
    print(f"  File: {entry.filename} ({len(file_data)} bytes)")
    print()
    hexdump(file_data)
    return True


def cmd_parse(data: bytes, entries: list[BupDirEntry], filename: str, fmt: str):
    """Parse a file using a known format."""
    entry = find_entry_by_name(entries, filename)
    if entry is None:
        print(f"Error: File '{filename}' not found in BUP image", file=sys.stderr)
        print(f"Available files: {', '.join(e.filename for e in entries)}", file=sys.stderr)
        return False

    file_data = extract_file_data(data, entry)

    # Auto-detect format
    if fmt == "auto":
        fmt = detect_format(file_data)
        if fmt is None:
            print(f"  File: {entry.filename} ({len(file_data)} bytes)")
            print(f"  Format: unknown (magic: {file_data[:4].hex() if len(file_data) >= 4 else 'N/A'})")
            print()
            print("  Raw hex dump:")
            hexdump(file_data[:256])
            if len(file_data) > 256:
                print(f"  ... ({len(file_data) - 256} more bytes)")
            return True

    parser = FORMAT_PARSERS.get(fmt)
    if parser is None:
        print(f"Error: Unknown format '{fmt}'", file=sys.stderr)
        print(f"Available formats: {', '.join(FORMAT_PARSERS.keys())}", file=sys.stderr)
        return False

    print(f"  File: {entry.filename} ({len(file_data)} bytes)")
    print()

    parsed = parser(file_data)
    display = DISPLAY_FUNCS.get(fmt)
    if display:
        display(parsed)
    else:
        # Generic display
        for key, value in parsed.items():
            print(f"  {key}: {value}")

    return True


def cmd_all(data: bytes, path: Path):
    """List all files and parse with auto-detected formats."""
    if not parse_bup_header(data):
        print(f"Error: {path} is not a valid BUP image", file=sys.stderr)
        return False

    entries = scan_directory(data)
    if not entries:
        print("  (no files)")
        return True

    for i, entry in enumerate(entries):
        if i > 0:
            print()
            print("  " + "=" * 60)
            print()

        file_data = extract_file_data(data, entry)
        fmt = detect_format(file_data)

        print(f"  File: {entry.filename}  Size: {entry.datasize} bytes  "
              f"Blocks: {len(entry.chain)}  Date: {entry.format_date()}")
        if entry.comment:
            print(f"  Comment: {entry.comment}")

        if fmt:
            print(f"  Format: {fmt}")
            print()
            parser = FORMAT_PARSERS.get(fmt)
            if parser:
                parsed = parser(file_data)
                display = DISPLAY_FUNCS.get(fmt)
                if display:
                    display(parsed)
        else:
            magic_str = file_data[:4].hex() if len(file_data) >= 4 else "N/A"
            print(f"  Format: unknown (magic: {magic_str})")

    return True


# ============================================================================
# Vmem (.BUP) Commands
# ============================================================================


def vmem_cmd_info(vmem: VmemFile, path: Path, raw_size: int):
    """Show Vmem file info."""
    print(f"File:       {path}")
    print(f"Size:       {raw_size} bytes (Satiator Vmem export)")
    print(f"Filename:   {vmem.filename}")
    print(f"Comment:    {vmem.comment}")
    print(f"Date:       {vmem.format_date()}")
    print(f"Data size:  {vmem.datasize} bytes")


def vmem_cmd_list(vmem: VmemFile, path: Path):
    """List the single file in a Vmem export."""
    print(f"File: {path} (Satiator Vmem)")
    print(f"  {'Filename':<12s} {'Size':>6s}  {'Date':<18s} {'Comment'}")
    print(f"  {'-'*12} {'-'*6}  {'-'*18} {'-'*10}")
    print(f"  {vmem.filename:<12s} {vmem.datasize:>6d}  {vmem.format_date():<18s} {vmem.comment}")


def process_vmem_file(data: bytes, path: Path, args) -> int:
    """Process a Vmem (.BUP) single-file export."""
    vmem = VmemFile(data)
    if not vmem.is_valid:
        print(f"Error: {path} has no data", file=sys.stderr)
        return 1

    file_data = vmem._file_data

    if args.info:
        vmem_cmd_info(vmem, path, len(data))
    elif args.list:
        vmem_cmd_list(vmem, path)
    elif args.extract:
        if args.output:
            Path(args.output).write_bytes(file_data)
            print(f"Extracted {len(file_data)} bytes to {args.output}")
        else:
            sys.stdout.buffer.write(file_data)
    elif args.hexdump:
        print(f"  File: {vmem.filename} ({len(file_data)} bytes)")
        print()
        hexdump(file_data)
    elif args.parse:
        fmt = args.format
        if fmt == "auto":
            fmt = detect_format(file_data)
        print(f"  File: {vmem.filename} ({len(file_data)} bytes)")
        print()
        if fmt and fmt in FORMAT_PARSERS:
            parsed = FORMAT_PARSERS[fmt](file_data)
            display = DISPLAY_FUNCS.get(fmt)
            if display:
                display(parsed)
        else:
            magic = file_data[:4].hex() if len(file_data) >= 4 else "N/A"
            print(f"  Format: unknown (magic: {magic})")
            print()
            hexdump(file_data[:256])
            if len(file_data) > 256:
                print(f"  ... ({len(file_data) - 256} more bytes)")
    elif args.all:
        vmem_cmd_list(vmem, path)
        print()
        fmt = detect_format(file_data)
        if fmt and fmt in FORMAT_PARSERS:
            print(f"  Format: {fmt}")
            print()
            parsed = FORMAT_PARSERS[fmt](file_data)
            display = DISPLAY_FUNCS.get(fmt)
            if display:
                display(parsed)
        else:
            magic = file_data[:4].hex() if len(file_data) >= 4 else "N/A"
            print(f"  Format: unknown (magic: {magic})")

    return 0


# ============================================================================
# File Selection
# ============================================================================


def sort_files_by_mtime(paths: list[Path]) -> list[Path]:
    """Sort paths by modification time, newest first."""
    existing = [p for p in paths if p.exists()]
    return sorted(existing, key=lambda p: p.stat().st_mtime, reverse=True)


# ============================================================================
# Main
# ============================================================================


def main():
    parser = argparse.ArgumentParser(
        description="Saturn Backup RAM (.bkr) save file reader",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
examples:
  %(prog)s file.bkr --info
  %(prog)s file.bkr --list
  %(prog)s file.bkr --extract NLTEST_LOG --output out.bin
  %(prog)s file.bkr --hexdump NLTEST_LOG
  %(prog)s file.bkr --parse NLTEST_LOG
  %(prog)s file.bkr --parse NLTEST_LOG --format netlink
  %(prog)s file.bkr --all
  %(prog)s ~/.mednafen/sav/game.*.bkr --list
  %(prog)s ~/.mednafen/sav/game.*.bkr --all --all-files
        """,
    )

    parser.add_argument('bkr_files', type=Path, nargs='+', metavar='BKR_FILE',
                        help='Path(s) to .bkr file(s)')

    # Commands
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--info', action='store_true',
                       help='Show BUP filesystem summary')
    group.add_argument('--list', action='store_true',
                       help='List all files in backup RAM')
    group.add_argument('--extract', metavar='FILENAME',
                       help='Extract a file\'s raw data')
    group.add_argument('--hexdump', metavar='FILENAME',
                       help='Hex dump a file\'s data')
    group.add_argument('--parse', metavar='FILENAME',
                       help='Parse a file using a known format')
    group.add_argument('--all', action='store_true',
                       help='List and parse all files with auto-detected formats')

    # Options
    parser.add_argument('--format', choices=list(FORMAT_PARSERS.keys()) + ['auto'],
                        default='auto',
                        help='Format for --parse (default: auto)')
    parser.add_argument('--output', '-o', metavar='FILE',
                        help='Output file for --extract (default: stdout)')
    parser.add_argument('--all-files', action='store_true',
                        help='Process all .bkr files (default: newest only)')

    args = parser.parse_args()

    # Default action
    if not any([args.info, args.list, args.extract, args.hexdump, args.parse, args.all]):
        args.info = True

    # Resolve and sort files
    paths = sort_files_by_mtime(args.bkr_files)
    if not paths:
        print("Error: No .bkr files found", file=sys.stderr)
        return 1

    if not args.all_files:
        paths = paths[:1]

    exit_code = 0

    for i, path in enumerate(paths):
        if len(paths) > 1:
            if i > 0:
                print()
                print("=" * 70)
                print()

        try:
            data = path.read_bytes()
        except Exception as e:
            print(f"Error reading {path}: {e}", file=sys.stderr)
            exit_code = 1
            continue

        # Detect file format and route accordingly
        if is_vmem_file(data):
            exit_code = process_vmem_file(data, path, args)
            continue

        if len(data) != BUP_FILE_SIZE:
            print(f"Warning: {path} is {len(data)} bytes "
                  f"(expected {BUP_FILE_SIZE})", file=sys.stderr)

        if args.info:
            cmd_info(data, path)
        elif args.list:
            print(f"File: {path}")
            if not cmd_list(data, path):
                exit_code = 1
        elif args.extract:
            if not parse_bup_header(data):
                print(f"Error: {path} is not a valid BUP image", file=sys.stderr)
                exit_code = 1
                continue
            entries = scan_directory(data)
            if not cmd_extract(data, entries, args.extract, args.output):
                exit_code = 1
        elif args.hexdump:
            if not parse_bup_header(data):
                print(f"Error: {path} is not a valid BUP image", file=sys.stderr)
                exit_code = 1
                continue
            entries = scan_directory(data)
            if not cmd_hexdump(data, entries, args.hexdump):
                exit_code = 1
        elif args.parse:
            if not parse_bup_header(data):
                print(f"Error: {path} is not a valid BUP image", file=sys.stderr)
                exit_code = 1
                continue
            entries = scan_directory(data)
            if not cmd_parse(data, entries, args.parse, args.format):
                exit_code = 1
        elif args.all:
            print(f"File: {path}")
            if not cmd_all(data, path):
                exit_code = 1

    return exit_code


if __name__ == '__main__':
    sys.exit(main())
