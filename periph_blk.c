#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/workqueue.h>
#include <linux/vmalloc.h>
#include <linux/blk-mq.h>
#include <linux/fs.h>
#include <linux/blk_types.h>
#include <linux/string.h>
#include <linux/kernel.h>

// Encryption function
static void encrypt_buffer(char *buf, int key)
{
    int i;
	size_t len = strlen(buf);
    for (i = 0; i < len; i++) {
        if (buf[i] >= 'a' && buf[i] <= 'z') {
            buf[i] = 'a' + ((buf[i] - 'a' + key) % 26);
        } else if (buf[i] >= 'A' && buf[i] <= 'Z') {
            buf[i] = 'A' + ((buf[i] - 'A' + key) % 26);
        }
    }
}

// Decryption function
static void decrypt_buffer(char *buf, int key)
{
    int i;
	size_t len = strlen(buf);
    for (i = 0; i < len; i++) {
        if (buf[i] >= 'a' && buf[i] <= 'z') {
            buf[i] = 'a' + ((buf[i] - 'a' - key + 26) % 26);
        } else if (buf[i] >= 'A' && buf[i] <= 'Z') {
            buf[i] = 'A' + ((buf[i] - 'A' - key + 26) % 26);
        }
    }
}

struct blk_device {
	sector_t                capacity;
	struct gendisk		*blk_disk;
	struct blk_mq_tag_set   tag_set;
	struct file              *data;
	const char 			*file_path;
	int 			encryption_key;
};

static int copy_to_blk(struct blk_device *blk, struct bio_vec *bvec,
			loff_t offset)
{
	unsigned int size;
	void *tmp;

	pr_info("periph_blk: copy_to_blk (offset = %llu)\n", offset);

	// Recupere la taille
	size = bvec->bv_len;
	
	// On alloue le buffer
	tmp = kzalloc(sizeof(char) * (size_t) size, GFP_KERNEL);
	if (tmp == NULL) {
		pr_info("periph_blk: Error in allocation\n");
		return 0;
	}

	// Recupere les informations dans le bio_vec
	memcpy_from_bvec(tmp, bvec);

	encrypt_buffer(tmp, blk->encryption_key);

	// Ecrit les informations récupérées dans le fichier
	kernel_write(blk->data, tmp, size, &offset);

	kfree(tmp);
	return 0;
}

static int copy_from_blk(struct bio_vec *bvec, struct blk_device *blk,
			loff_t offset)
{
	unsigned int size;
	void *tmp;

	pr_info("periph_blk: copy_from_blk (offset = %llu)\n", offset);

	// Recupere la taille
	size = bvec->bv_len;
	
	// On alloue le buffer
	tmp = kzalloc(sizeof(char) * (size_t) size, GFP_KERNEL);
	if (tmp == NULL) {
		pr_info("periph_blk: Error in allocation\n");
		return 0;
	}

	// Recupere les informations dans le fichier
	kernel_read(blk->data, tmp, size , &offset);

	decrypt_buffer(tmp, blk->encryption_key);

	// Copie les informations récupérées dans le bio_vec
	memcpy_to_bvec(bvec, tmp);

	kfree(tmp);
	return 0;
}

static int blk_do_bvec(struct blk_device *blk, struct bio_vec *bvec,
		unsigned int op, loff_t offset)
{
	int err = 0;
	switch (op) {
	case REQ_OP_READ:
		err = copy_from_blk(bvec, blk, offset);
		break;
	case REQ_OP_WRITE:
		err = copy_to_blk(blk, bvec, offset);
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

struct blk_rq_worker {
	struct work_struct work;
	struct blk_device *blk;
	struct request *rq;
};

static void mrd_rq_worker_workfn(struct work_struct *work)
{
	struct blk_rq_worker *worker =
		container_of(work, struct blk_rq_worker, work);
	struct request *rq = worker->rq;
	blk_status_t err = BLK_STS_OK;
	struct bio_vec bvec;
	struct req_iterator iter;
	loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;
	struct blk_device *blk = worker->blk;
	loff_t data_len = blk->capacity << SECTOR_SHIFT;
	unsigned int nr_bytes = 0;

	blk_mq_start_request(rq);

	rq_for_each_segment(bvec, rq, iter) {
		unsigned int len = bvec.bv_len;
		int err_do_bvec;

		if (pos + len > data_len) {
			err = BLK_STS_IOERR;
			break;
		}

		err_do_bvec = blk_do_bvec(blk, &bvec, req_op(rq), pos);

		if (err_do_bvec) {
			err = BLK_STS_IOERR;
			goto end_request;
		}
		pos += len;
		nr_bytes += len;
	}

end_request:
	blk_mq_end_request(rq, err);
	module_put(THIS_MODULE);
	kfree(worker);
}

static struct workqueue_struct *workqueue;

static blk_status_t blk_queue_rq(struct blk_mq_hw_ctx *hctx,
				const struct blk_mq_queue_data *bd)
{
	struct blk_rq_worker *worker;

	if (!try_module_get(THIS_MODULE)) {
		pr_err("periph_blk: unable to get module");
		return BLK_STS_IOERR;
	}

	worker = kzalloc(sizeof *worker, GFP_KERNEL);
	if (!worker) {
		pr_err("periph_blk: cannot allocate worker\n");
		module_put(THIS_MODULE);
		return BLK_STS_IOERR;
	}
	worker->blk = hctx->queue->queuedata;
	worker->rq = bd->rq;
	INIT_WORK(&worker->work, mrd_rq_worker_workfn);
	queue_work(workqueue, &worker->work);

	return BLK_STS_OK;
}

static const struct blk_mq_ops block_mq_ops = {
	.queue_rq = blk_queue_rq,
};

static const struct block_device_operations block_fops = {
	.owner =		THIS_MODULE,
};

/*
 * And now the modules code and kernel interface.
 */
static char *file_name = "/tmp/periph_blk";
module_param(file_name, charp, 0444);
MODULE_PARM_DESC(filename, "Name of the tmp file use");

static int private_key = 3;
module_param(private_key, int, 0444);
MODULE_PARM_DESC(private_key, "Private key for encryption");

static unsigned long blk_size = 50 * 1024;
module_param(blk_size, ulong, 0444);
MODULE_PARM_DESC(blk_size, "Size of each block disk in kbytes.");

static int max_part = 16;
module_param(max_part, int, 0444);
MODULE_PARM_DESC(max_part, "Num Minors for each devices");

static unsigned int major;

static struct blk_device *blk;

static int blk_alloc(const char *filename, int flags, umode_t mode, int key)
{
	struct gendisk *disk;
	int err = -ENOMEM;
	char buf[DISK_NAME_LEN];

	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk) {
		err= -ENOMEM;
		goto out;
	}

	snprintf(buf, DISK_NAME_LEN, "periph_blk");

	blk->data = filp_open(filename, flags, mode);
	if (!blk->data) {
		pr_err("periph_blk: problem with open file\n");
		err = -ENOMEM;
		goto out_free_dev;
	}

	blk->file_path=filename;
	blk->encryption_key=key;

	blk->capacity = blk_size * 2;
	memset(&blk->tag_set, 0, sizeof(blk->tag_set));
	blk->tag_set.ops = &block_mq_ops;
	blk->tag_set.queue_depth = 128;
	blk->tag_set.numa_node = NUMA_NO_NODE;
	blk->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	blk->tag_set.cmd_size = 0;
	blk->tag_set.driver_data = blk;
	blk->tag_set.nr_hw_queues = 1;
	err = blk_mq_alloc_tag_set(&blk->tag_set);
	if (err) {
		goto out_free_data;
	}

	disk = blk->blk_disk =blk_mq_alloc_disk(&blk->tag_set, blk);
	if (IS_ERR(disk)) {
		err = PTR_ERR(disk);
		pr_err("periph_blk: error allocating disk\n");
		goto out_free_tagset;
	}

	disk->major = major;
	disk->first_minor	= 0;
	disk->minors		= max_part;
	disk->fops		= &block_fops;
	disk->private_data	= blk;
	strlcpy(disk->disk_name, buf, DISK_NAME_LEN);
	set_capacity(disk, blk_size * 2);

	blk_queue_physical_block_size(disk->queue, PAGE_SIZE);

	/* Tell the block layer that this is not a rotational device */
	blk_queue_flag_set(QUEUE_FLAG_NONROT, disk->queue);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, disk->queue);
	err = add_disk(disk);
	if (err)
		goto out_cleanup_disk;

	pr_info("periph_blk: new disk, major = %d, first_minor = %d, minors = %d\n",
		disk->major, disk->first_minor, disk->minors);

	return 0;

out_cleanup_disk:
	put_disk(disk);
out_free_tagset:
	blk_mq_free_tag_set(&blk->tag_set);
// out_free_filename:
// 	vfree(blk->file_path);
out_free_data:
	filp_close(blk->data, NULL);
	vfree(blk->data);
out_free_dev:
	kfree(blk);
out:
	return err;
}

static void blk_cleanup(void)
{
	del_gendisk(blk->blk_disk);
	put_disk(blk->blk_disk);
	vfree(blk->data);
	kfree(blk);
}

static int __init blk_init(void)
{
	int err;

	// Flags pour le fichier, ici flag en écriture/lecture
	// et création si le fichier n'est pas créé.
	int flags = O_RDWR | O_CREAT;
	// Mode si le fichier se créé :
	umode_t mode = 0777; // -rwxr-xr-x

	if ((major = register_blkdev(0, "periph_blk")) < 0) {
		err = -EIO;
		goto out;
	}

	workqueue = alloc_workqueue("periph_blk", WQ_MEM_RECLAIM, 1);
	if (!workqueue) {
		err = -ENOMEM;
		goto out_workqueue;
	}

	err = blk_alloc(file_name, flags, mode, private_key);
	if (err) {
		goto out_free;
	}

	pr_info("periph_blk: module loaded\n");
	return 0;

out_free:
	destroy_workqueue(workqueue);
out_workqueue:
	unregister_blkdev(major, "periph_blk");
out:
	pr_info("periph_blk: module NOT loaded !!!\n");
	return err;
}

static void __exit blk_exit(void)
{
	unregister_blkdev(major, "periph_blk");
	blk_cleanup();
	destroy_workqueue(workqueue);

	pr_info("periph_blk: module unloaded\n");
}

module_init(blk_init);
module_exit(blk_exit);

MODULE_LICENSE ("GPL");
MODULE_AUTHOR("Richard DUFOUR");
MODULE_ALIAS("periph_blk");