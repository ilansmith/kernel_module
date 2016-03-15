# kernel_module
A simple block device kernel module implementation example.

The module allocates a 100MB buffer in RAM which is accessible (read/write) via a block device:

    /dev/ias_blkdev

To get detailed comentry about the block device's flow in the kernel log define `IAS_DEBUG` in ias_blkdev.c.

If the kernel is configured with `CONFIG_DEBUG_FS=y`, then building with `IAS_DEBUG` defined will also result in creating a 
debugfs dump file to directly export (read) the content of the RAM buffer:

    /sys/kernel/debug/ias/dump

Use it together with xxd to get a human readable hexdump representation of the buffer:

    cat /sys/kernel/debug/ias/dump | xxd | less
