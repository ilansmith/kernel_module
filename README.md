# IAS Block Device Kernel Module

A simple RAM-backed block device kernel module for Linux kernel 5.15.

## Overview

This kernel module creates a virtual block device that stores all data in RAM. It serves as an educational example demonstrating:

- Linux block device driver architecture
- Bio-based I/O processing using `submit_bio`
- Kernel memory allocation with `vmalloc`
- Optional debugfs interface for debugging

### How It Works

```
┌─────────────────────────────────────────────────────────┐
│                    User Space                           │
│        (mkfs, mount, read, write, etc.)                 │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│              Linux Block Layer                          │
│           (creates bio structures)                      │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│              ias_blkdev module                          │
│                                                         │
│   submit_bio() ──► bio_for_each_segment()               │
│                           │                             │
│                           ▼                             │
│                    ias_transfer()                       │
│                           │                             │
│                           ▼                             │
│              memcpy to/from RAM buffer                  │
│                                                         │
│   ┌─────────────────────────────────────────────────┐   │
│   │     physical_dev (RAM buffer - 100MB default)   │   │
│   │     [sector 0][sector 1][sector 2]...[sector N] │   │
│   └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

When loaded, the module:
1. Registers a block device with the kernel
2. Allocates a RAM buffer (default 100MB) using `vmalloc`
3. Creates `/dev/ias_blkdev` - a block device backed by this RAM buffer
4. All reads/writes to the device are translated to `memcpy` operations on the buffer

**Note:** Data is volatile - it persists while the module is loaded but is lost when the module is unloaded or the system reboots.

## The Block Device

Once loaded, the module creates:

```
/dev/ias_blkdev
```

This device behaves like any standard block device and can be:
- Partitioned (though not typically needed for a RAM disk)
- Formatted with any filesystem (ext4, xfs, btrfs, etc.)
- Mounted and used for file storage
- Accessed directly for raw block I/O

### Device Properties

| Property | Value |
|----------|-------|
| Default Size | 100 MB |
| Sector Size | 512 bytes |
| Device Name | `ias_blkdev` |
| Device Path | `/dev/ias_blkdev` |

## DebugFS Dump File

When built with debug support (see [Building with DebugFS](#building-with-debugfs-support)), the module creates:

```
/sys/kernel/debug/ias/dump
```

This file provides **direct read-only access** to the raw contents of the RAM buffer, bypassing the filesystem layer entirely. This is useful for:

- Debugging filesystem corruption
- Examining raw block data
- Verifying data written to specific sectors
- Educational purposes (seeing exactly what's stored)

## Building

### Prerequisites

- Linux kernel headers for your running kernel
- Build tools (`make`, `gcc`)
- Root access for loading the module

```bash
# Debian/Ubuntu
sudo apt install build-essential linux-headers-$(uname -r)

# RHEL/CentOS/Fedora
sudo dnf install kernel-devel make gcc
```

### Standard Build

```bash
make
```

This produces `ias_blkdev.ko`.

### Building with DebugFS Support

To enable debug logging and the debugfs dump file, edit `ias_blkdev.c` and change:

```c
#if 0
#define IAS_DEBUG
#endif
```

to:

```c
#if 1
#define IAS_DEBUG
#endif
```

Then rebuild:

```bash
make clean
make
```

**Requirements:** Your kernel must be configured with `CONFIG_DEBUG_FS=y` for the debugfs dump file to be created.

### Clean Build Artifacts

```bash
make clean      # Remove build files
make cleanall   # Remove all generated files including tags
```

## Loading and Unloading

### Load the Module

```bash
# Load with default size (100MB)
sudo insmod ias_blkdev.ko

# Load with custom size (e.g., 50MB)
sudo insmod ias_blkdev.ko size=52428800

# Load with custom size (e.g., 200MB)
sudo insmod ias_blkdev.ko size=$((200 * 1024 * 1024))
```

### Verify Module is Loaded

```bash
# Check module is loaded
lsmod | grep ias_blkdev

# Check device exists
ls -la /dev/ias_blkdev

# Check device size
sudo blockdev --getsize64 /dev/ias_blkdev
```

### Unload the Module

```bash
# Make sure device is not in use first
sudo umount /mnt/ramdisk 2>/dev/null

# Unload
sudo rmmod ias_blkdev
```

## Usage Examples

### Example 1: Raw Block Device Access

Write and read data directly to/from the block device:

```bash
# Write a string to the first sector
echo "Hello, Block Device!" | sudo dd of=/dev/ias_blkdev bs=512 count=1

# Read it back
sudo dd if=/dev/ias_blkdev bs=512 count=1 2>/dev/null | head -c 50
echo

# View as hexdump
sudo dd if=/dev/ias_blkdev bs=512 count=1 2>/dev/null | xxd | head -5
```

Write binary pattern and verify:

```bash
# Write pattern to first 1MB
sudo dd if=/dev/urandom of=/dev/ias_blkdev bs=1M count=1

# Calculate checksum
sudo dd if=/dev/ias_blkdev bs=1M count=1 2>/dev/null | md5sum

# Write zeros
sudo dd if=/dev/zero of=/dev/ias_blkdev bs=1M count=10

# Verify zeros
sudo dd if=/dev/ias_blkdev bs=1M count=1 2>/dev/null | xxd | head
```

### Example 2: Create and Use a Filesystem

```bash
# Create ext4 filesystem
sudo mkfs.ext4 /dev/ias_blkdev

# Create mount point
sudo mkdir -p /mnt/ramdisk

# Mount the filesystem
sudo mount /dev/ias_blkdev /mnt/ramdisk

# Check mounted filesystem
df -h /mnt/ramdisk
mount | grep ias_blkdev

# Use it like any filesystem
sudo bash -c 'echo "Hello from RAM disk!" > /mnt/ramdisk/hello.txt'
cat /mnt/ramdisk/hello.txt

# Create some files
sudo dd if=/dev/urandom of=/mnt/ramdisk/random.bin bs=1M count=5
sudo cp /etc/passwd /mnt/ramdisk/
ls -la /mnt/ramdisk/

# Check space usage
df -h /mnt/ramdisk

# Unmount when done
sudo umount /mnt/ramdisk
```

### Example 3: Data Persistence Test

Data persists while module is loaded:

```bash
# Mount and write data
sudo mount /dev/ias_blkdev /mnt/ramdisk
sudo bash -c 'echo "Persistent data" > /mnt/ramdisk/test.txt'
sudo umount /mnt/ramdisk

# Remount - data should still be there
sudo mount /dev/ias_blkdev /mnt/ramdisk
cat /mnt/ramdisk/test.txt   # Output: Persistent data
sudo umount /mnt/ramdisk

# Unload and reload module - data is LOST
sudo rmmod ias_blkdev
sudo insmod ias_blkdev.ko
sudo mount /dev/ias_blkdev /mnt/ramdisk   # Will fail - no filesystem
```

### Example 4: Using the DebugFS Dump File

**Note:** Requires building with `IAS_DEBUG` defined and kernel `CONFIG_DEBUG_FS=y`.

```bash
# Check if debugfs is mounted
mount | grep debugfs
# If not mounted:
sudo mount -t debugfs none /sys/kernel/debug

# Check dump file exists
ls -la /sys/kernel/debug/ias/dump

# View raw buffer contents as hexdump
sudo cat /sys/kernel/debug/ias/dump | xxd | head -50

# View with less for navigation
sudo cat /sys/kernel/debug/ias/dump | xxd | less

# Search for a string in the raw buffer
sudo cat /sys/kernel/debug/ias/dump | xxd | grep -i "hello"

# Compare specific region with dd
# First 512 bytes via debugfs:
sudo dd if=/sys/kernel/debug/ias/dump bs=512 count=1 2>/dev/null | xxd

# First 512 bytes via block device:
sudo dd if=/dev/ias_blkdev bs=512 count=1 2>/dev/null | xxd

# Dump entire buffer to file for analysis
sudo cat /sys/kernel/debug/ias/dump > /tmp/ramdisk_dump.bin
ls -la /tmp/ramdisk_dump.bin

# Find specific byte patterns
sudo cat /sys/kernel/debug/ias/dump | xxd | grep "ef be ad de"
```

### Example 5: Performance Testing

```bash
# Mount filesystem
sudo mkfs.ext4 /dev/ias_blkdev
sudo mount /dev/ias_blkdev /mnt/ramdisk

# Write performance test
sudo dd if=/dev/zero of=/mnt/ramdisk/testfile bs=1M count=50 conv=fdatasync
# Expected: Very fast (RAM speed, hundreds of MB/s)

# Read performance test  
sudo dd if=/mnt/ramdisk/testfile of=/dev/null bs=1M
# Expected: Very fast (RAM speed)

# Cleanup
sudo umount /mnt/ramdisk
```

### Example 6: Using with Different Filesystems

```bash
# XFS
sudo mkfs.xfs -f /dev/ias_blkdev
sudo mount /dev/ias_blkdev /mnt/ramdisk
df -T /mnt/ramdisk
sudo umount /mnt/ramdisk

# Btrfs
sudo mkfs.btrfs -f /dev/ias_blkdev
sudo mount /dev/ias_blkdev /mnt/ramdisk
df -T /mnt/ramdisk
sudo umount /mnt/ramdisk

# FAT32 (useful for USB-like testing)
sudo mkfs.vfat /dev/ias_blkdev
sudo mount /dev/ias_blkdev /mnt/ramdisk
df -T /mnt/ramdisk
sudo umount /mnt/ramdisk
```

## Troubleshooting

### Module fails to load

```bash
# Check dmesg for errors
dmesg | tail -20

# Verify kernel headers match running kernel
uname -r
ls /lib/modules/$(uname -r)/build
```

### Device not appearing

```bash
# Check if module loaded
lsmod | grep ias

# Check kernel log
dmesg | grep ias_blkdev
```

### Cannot unmount

```bash
# Check what's using the device
sudo lsof /mnt/ramdisk
sudo fuser -m /mnt/ramdisk

# Force unmount (use with caution)
sudo umount -l /mnt/ramdisk
```

## Module Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `size` | uint | 104857600 (100MB) | Size of RAM buffer in bytes |

Example:
```bash
# 50MB RAM disk
sudo insmod ias_blkdev.ko size=52428800

# 256MB RAM disk
sudo insmod ias_blkdev.ko size=268435456
```

## Files

| File | Description |
|------|-------------|
| `ias_blkdev.c` | Main driver source code |
| `Makefile` | Build configuration |
| `README.md` | This documentation |

## Kernel Compatibility

This version is compatible with **Linux kernel 5.15**. Key APIs used:

- `blk_alloc_disk()` - Allocates gendisk and queue together
- `submit_bio` callback - Bio-based I/O processing
- `bio_for_each_segment()` - Iterating bio segments
- `kmap_local_page()` - Page mapping for data access

## License

GPL (GNU General Public License)

## Author

Ilan A. Smith
