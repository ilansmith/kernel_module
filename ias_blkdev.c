#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <uapi/linux/hdreg.h>

#if 0
#define IAS_DEBUG
#endif

#ifdef IAS_DEBUG
#define ias_dbg(_fmt_, ...) pr_err(" " IAS_BLKDEV_NAME ": %s(): " _fmt_ "\n", \
	__FUNCTION__, ##__VA_ARGS__)

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

#define IAS_DEBUG_FS
#endif
#else
#define ias_dbg(_fmt_, ...)
#undef IAS_DEBUG_FS
#endif

MODULE_AUTHOR ("Ilan A. Smith");
MODULE_DESCRIPTION("Sample block device");
MODULE_LICENSE ("GPL");

#define KB(sz) ((sz) * 1024)
#define MB(sz) (KB(sz) * 1024)

#define IAS_BLKDEV_NAME "ias_blkdev"
#define IAS_BLKDEV_DEFAULT_SIZE MB(100)
#define IAS_MINOR_FIRST 0
#define IAS_MINOR_CNT 16
#define IAS_DEBUGFS_DIR "ias"

#ifdef IAS_DEBUG_FS
#define ias_blkdev_init_debugfs_feature(_name_, _open_, _reader_, \
	_writer_, _seeker_, _releaser_) \
	static const struct file_operations fops_debug_##_name_ = { \
		.owner = THIS_MODULE, \
		.open = _open_, \
		.read = _reader_, \
		.write = _writer_, \
		.llseek = _seeker_, \
		.release = _releaser_ \
	}; \
	static int ias_blkdev_init_debugfs_##_name_(struct dentry *dir, \
		void *data) \
	{ \
		void *rc; \
		\
		rc = debugfs_create_file(#_name_, 0222, dir, data, \
			&fops_debug_##_name_); \
		if (IS_ERR_OR_NULL(rc)) { \
			ias_dbg("Failed to create " #_name_ \
			" debugfs entry - %ld", PTR_ERR(rc)); \
			return -1; \
		} \
		ias_dbg("Debugfs entry '" #_name_ "' created successfuly"); \
		return 0; \
	}
#endif

#define BYTES_TO_SECTORS(bytes) ((bytes) >> SECTOR_SHIFT)
#define SECTORS_TO_BYTES(sectors) ((sectors) << SECTOR_SHIFT)

#define IAS_SECTOR_SIZE 512

struct ias_blkdev {
	int major;
	u8 *physical_dev;
	u32 physical_size;
	spinlock_t dlock;
	struct gendisk *disk;
#ifdef IAS_DEBUG_FS
	struct dentry *debugfs_dir;
#endif
};

static struct ias_blkdev ibd;

/* device parameters */
static u32 size = IAS_BLKDEV_DEFAULT_SIZE;
module_param(size, uint, S_IRUGO);

#ifdef IAS_DEBUG_FS
/* debugfs - dump sequential file operations */
static void *ias_blkdev_dump_seq_start(struct seq_file *sfile,
	loff_t *pos)
{
	struct ias_blkdev *bd = (struct ias_blkdev*)sfile->private;

	return *pos < bd->physical_size - 1 ? pos : NULL;
}

static void *ias_blkdev_dump_seq_next(struct seq_file *sfile, void *v,
	loff_t *pos)
{
	struct ias_blkdev *bd = (struct ias_blkdev*)sfile->private;

	(*pos)++;
	return *pos < bd->physical_size - 1 ? pos : NULL;
}

static void ias_blkdev_dump_seq_stop(struct seq_file *sfile, void *v)
{
}

static int ias_blkdev_dump_seq_show(struct seq_file *sfile, void *v)
{
	struct ias_blkdev *bd = (struct ias_blkdev*)sfile->private;
	loff_t *pos = (loff_t*)v;

	seq_putc(sfile, bd->physical_dev[*pos]);
	return 0;
}

static struct seq_operations sops_debug_dump = {
	.start = ias_blkdev_dump_seq_start,
	.next  = ias_blkdev_dump_seq_next,
	.stop  = ias_blkdev_dump_seq_stop,
	.show  = ias_blkdev_dump_seq_show,
};

/* debugfs - ias/dump file operations */
static int ias_debugfs_dump_open(struct inode *inode, struct file *file)
{
	struct ias_blkdev *bd = (struct ias_blkdev*)inode->i_private;
	int ret;

	ret = seq_open(file, &sops_debug_dump);
	if (ret)
		return ret;

	((struct seq_file*)file->private_data)->private = bd;
	return ret;
}

static int ias_debugfs_dump_realse(struct inode *inode, struct file *file)
{
	return seq_release(inode, file);
}

ias_blkdev_init_debugfs_feature(dump, ias_debugfs_dump_open,
	seq_read, NULL, seq_lseek, ias_debugfs_dump_realse);

/* debugfs - init/exit */
static int ias_blkdev_init_debugfs(struct ias_blkdev *bd)
{
	bd->debugfs_dir = debugfs_create_dir(IAS_DEBUGFS_DIR, NULL);
	if (IS_ERR_OR_NULL(bd->debugfs_dir)) {
		ias_dbg("Cannot create debugfs entry: /sys/kernel/debug/"
			IAS_DEBUGFS_DIR ". Is debugfs configured?");
		return -1;
	}

	ias_dbg("Created debugfs directory '" IAS_DEBUGFS_DIR "'");

	ias_blkdev_init_debugfs_dump(bd->debugfs_dir, bd);
	return 0;
}

static void ias_blkdev_exit_debugfs(struct ias_blkdev *bd)
{
	if (IS_ERR_OR_NULL(bd->debugfs_dir))
		return;

	debugfs_remove_recursive(bd->debugfs_dir);
	ias_dbg("Removed debugfs directory '" IAS_DEBUGFS_DIR "'");
}
#else
static inline int ias_blkdev_init_debugfs(struct ias_blkdev *bd)
{
	return 0;
}

static inline void ias_blkdev_exit_debugfs(struct ias_blkdev *bd)
{
}
#endif

/* block device - bio processing */
static void ias_transfer(struct ias_blkdev *bd, sector_t sector,
	unsigned int len, char *buf, int write)
{
	u64 offset = SECTORS_TO_BYTES(sector);

	ias_dbg("Called with sector: %llu, len: %u, buf: 0x%p, write: %d",
		(unsigned long long)sector, len, buf, write);

	/* verify you're in limits */
	if (offset + len > bd->physical_size) {
		ias_dbg("Device overflow: offset + len: %llu, capacity: %u",
			(unsigned long long)(offset + len), bd->physical_size);
		return;
	}

	spin_lock_irq(&bd->dlock);
	if (write)
		memcpy(bd->physical_dev + offset, buf, len);
	else
		memcpy(buf, bd->physical_dev + offset, len);
	spin_unlock_irq(&bd->dlock);
}

static blk_qc_t ias_submit_bio(struct bio *bio)
{
	struct ias_blkdev *bd = bio->bi_bdev->bd_disk->private_data;
	struct bio_vec bvec;
	struct bvec_iter iter;
	sector_t sector = bio->bi_iter.bi_sector;
	int write = bio_data_dir(bio) == WRITE;

	ias_dbg("Called with bio: 0x%p, sector: %llu, write: %d",
		bio, (unsigned long long)sector, write);

	bio_for_each_segment(bvec, bio, iter) {
		char *buf = kmap_local_page(bvec.bv_page) + bvec.bv_offset;
		unsigned int len = bvec.bv_len;

		ias_transfer(bd, sector, len, buf, write);
		kunmap_local(buf);

		sector += BYTES_TO_SECTORS(len);
	}

	bio_endio(bio);
	return BLK_QC_T_NONE;
}

static int ias_blkdev_open(struct block_device *bdev, fmode_t mode)
{
	ias_dbg("Called with bdev: 0x%p, mode: 0x%x", bdev, mode);
	return 0;
}

static void ias_blkdev_release(struct gendisk *disk, fmode_t mode)
{
	ias_dbg("Called with disk: 0x%p, mode: 0x%x", disk, mode);
}

static int ias_blkdev_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	ias_dbg("Called with *bdev: 0x%p, *geo: 0x%p", bdev, geo);

	geo->heads = 4;
	geo->sectors = 16;
	geo->cylinders = get_capacity(bdev->bd_disk) /
		(geo->heads * geo->sectors);
	geo->start = 0;

	return 0;
}

static const struct block_device_operations ias_disk_fops = {
	.owner = THIS_MODULE,
	.open = ias_blkdev_open,
	.release = ias_blkdev_release,
	.getgeo = ias_blkdev_getgeo,
	.submit_bio = ias_submit_bio,
};

static int __init ias_blkdev_init(void)
{
	int err;

	/* register block device */
	ibd.major = register_blkdev(0, IAS_BLKDEV_NAME);
	if (ibd.major < 0)
		return -1;
	ias_dbg("Registered blkdev, major: %d", ibd.major);

	/* initialize pseudo physical device */
	ibd.physical_dev = (u8*)vmalloc(size);
	if (!ibd.physical_dev)
		goto error_unregister;
	memset(ibd.physical_dev, 0, size);
	ibd.physical_size = size;
	ias_dbg("Allocated pysical device: 0x%x bytes at 0x%p", size,
		ibd.physical_dev);

	/* initialize device spinlock */
	spin_lock_init(&ibd.dlock);
	ias_dbg("Initialized device spinlock");

	/* allocate disk - this also allocates the queue in kernel 5.15+ */
	ibd.disk = blk_alloc_disk(NUMA_NO_NODE);
	if (!ibd.disk)
		goto error_free_physical;
	ias_dbg("Allocated disk: 0x%p", ibd.disk);

	/* configure the disk */
	ibd.disk->major = ibd.major;
	ibd.disk->first_minor = IAS_MINOR_FIRST;
	ibd.disk->minors = IAS_MINOR_CNT;
	ibd.disk->fops = &ias_disk_fops;
	ibd.disk->private_data = (void*)&ibd;
	set_disk_ro(ibd.disk, 0);
	set_capacity(ibd.disk, BYTES_TO_SECTORS(size));
	strncpy(ibd.disk->disk_name, IAS_BLKDEV_NAME, DISK_NAME_LEN);

	/* set logical block size */
	blk_queue_logical_block_size(ibd.disk->queue, IAS_SECTOR_SIZE);
	ias_dbg("Set queue logical block size to: %u bytes", IAS_SECTOR_SIZE);

	ias_dbg("Initialized gendisk: 0x%p, minors: %d", ibd.disk,
		IAS_MINOR_CNT);

	/* add disk to the system - in 5.15 this returns an error code */
	err = add_disk(ibd.disk);
	if (err) {
		ias_dbg("Failed to add disk: %d", err);
		goto error_put_disk;
	}
	ias_dbg("Added disk");

	ias_blkdev_init_debugfs(&ibd);
	return 0;

error_put_disk:
	put_disk(ibd.disk);
error_free_physical:
	vfree(ibd.physical_dev);
error_unregister:
	unregister_blkdev(ibd.major, IAS_BLKDEV_NAME);

	return -1;
}

static void __exit ias_blkdev_exit(void)
{
	/* delete debugfs entries */
	ias_blkdev_exit_debugfs(&ibd);

	/* delete generic disk - this handles queue cleanup in 5.15+ */
	del_gendisk(ibd.disk);
	put_disk(ibd.disk);
	ias_dbg("Deleted gendisk");

	/* free physical memory */
	vfree(ibd.physical_dev);
	ias_dbg("Freed physical device");

	/* unregister block device */
	unregister_blkdev(ibd.major, IAS_BLKDEV_NAME);
	ias_dbg("Unregistered blkdev");
}

module_init(ias_blkdev_init);
module_exit(ias_blkdev_exit);

