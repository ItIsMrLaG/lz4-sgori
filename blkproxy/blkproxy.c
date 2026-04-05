// SPDX-License-Identifier: GPL-2.0-only
/*
 * blkproxy — простой прокси-драйвер блочного устройства для тестирования.
 *
 * Целевое ядро: 6.9+ (Fedora 42 / 6.12)
 *   - blk_alloc_disk(&lim, node)
 *   - bdev_file_open_by_path() -> struct file *
 *   - file_bdev(), fput()
 *
 * Использование:
 *   insmod blkproxy.ko
 *   echo -n /dev/sdb > /sys/kernel/blkproxy/create
 *   # → создаётся /dev/blkproxy
 *   echo 1 > /sys/kernel/blkproxy/destroy
 *   rmmod blkproxy
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/highmem.h>   /* kmap_local_page / kunmap_local */
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Block device proxy (test module)");
MODULE_AUTHOR("blkproxy test");

#define PROXY_NAME          "blkproxy"
/* 1 MiB в 512-байтовых секторах */
#define PROXY_MAX_SECTORS   2048U

/* ---------- глобальное состояние ---------- */

static int proxy_major;
static struct kobject *proxy_kobj;
static DEFINE_MUTEX(proxy_lock);

struct proxy_dev {
	struct gendisk      *disk;
	struct file         *bdev_file;      /* хэндл backing-устройства */
	struct block_device *backing_bdev;
};

static struct proxy_dev *g_pdev;        /* единственный экземпляр */

/* ---------- контекст одного bio-запроса ---------- */

struct proxy_bio_ctx {
	struct bio   *orig_bio;
	struct page **pages;
	unsigned int  nr_pages;
};

/* ---------- вспомогательные функции копирования ---------- */

/*
 * bio_copy_to_pages — скопировать данные из bio в плоский массив страниц.
 * Вызывается для WRITE перед отправкой вниз.
 */
static void bio_copy_to_pages(struct bio *bio, struct page **pages)
{
	struct bvec_iter iter = bio->bi_iter; /* не трогаем оригинал */
	struct bio_vec   bv;
	size_t dst_off = 0;

	bio_for_each_segment(bv, bio, iter) {
		size_t src_seg_off = bv.bv_offset;
		size_t rem         = bv.bv_len;

		while (rem > 0) {
			size_t pg_idx = dst_off >> PAGE_SHIFT;
			size_t pg_off = dst_off & (PAGE_SIZE - 1);
			size_t chunk  = min_t(size_t, rem, PAGE_SIZE - pg_off);
			void  *src, *dst;

			src = kmap_local_page(bv.bv_page) + src_seg_off;
			dst = kmap_local_page(pages[pg_idx]) + pg_off;
			memcpy(dst, src, chunk);
			/* kunmap в обратном порядке (LIFO) */
			kunmap_local(dst);
			kunmap_local(src);

			src_seg_off += chunk;
			dst_off     += chunk;
			rem         -= chunk;
		}
	}
}

/*
 * bio_copy_from_pages — скопировать данные из плоского массива страниц в bio.
 * Вызывается для READ в endio-колбэке.
 */
static void bio_copy_from_pages(struct page **pages, struct bio *bio)
{
	struct bvec_iter iter = bio->bi_iter;
	struct bio_vec   bv;
	size_t src_off = 0;

	bio_for_each_segment(bv, bio, iter) {
		size_t dst_seg_off = bv.bv_offset;
		size_t rem         = bv.bv_len;

		while (rem > 0) {
			size_t pg_idx = src_off >> PAGE_SHIFT;
			size_t pg_off = src_off & (PAGE_SIZE - 1);
			size_t chunk  = min_t(size_t, rem, PAGE_SIZE - pg_off);
			void  *src, *dst;

			src = kmap_local_page(pages[pg_idx]) + pg_off;
			dst = kmap_local_page(bv.bv_page) + dst_seg_off;
			memcpy(dst, src, chunk);
			kunmap_local(dst);
			kunmap_local(src);

			dst_seg_off += chunk;
			src_off     += chunk;
			rem         -= chunk;
		}
	}
}

/* ---------- endio нормализованного bio ---------- */

static void proxy_endio(struct bio *bio)
{
	struct proxy_bio_ctx *ctx = bio->bi_private;
	unsigned int i;

	/*
	 * READ завершён — копируем данные обратно в страницы исходного bio.
	 * WRITE: страницы уже не нужны, просто освобождаем.
	 */
	if (!bio->bi_status && bio_op(bio) == REQ_OP_READ)
		bio_copy_from_pages(ctx->pages, ctx->orig_bio);

	for (i = 0; i < ctx->nr_pages; i++)
		__free_pages(ctx->pages[i], 1);
	kfree(ctx->pages);

	ctx->orig_bio->bi_status = bio->bi_status;
	bio_endio(ctx->orig_bio);

	kfree(ctx);
	bio_put(bio);
}

/* ---------- submit_bio ---------- */

static void proxy_submit_bio(struct bio *orig_bio)
{
	struct proxy_dev     *pdev =
		orig_bio->bi_bdev->bd_disk->private_data;
	struct proxy_bio_ctx *ctx;
	struct bio           *new_bio;
	unsigned int          total = orig_bio->bi_iter.bi_size;
	unsigned int          nr_pages;
	unsigned int          i;

	/*
	 * Операции без данных (FLUSH, DISCARD, …) —
	 * просто пробрасываем через bio_chain.
	 */
	if (!bio_has_data(orig_bio)) {
		new_bio = bio_alloc(pdev->backing_bdev, 0,
				    orig_bio->bi_opf, GFP_NOIO);
		if (!new_bio)
			goto err_nomem;
		new_bio->bi_iter.bi_sector = orig_bio->bi_iter.bi_sector;
		bio_chain(new_bio, orig_bio);
		submit_bio_noacct(new_bio);
		return;
	}

	/* Сколько нормализованных страниц нам нужно */
	nr_pages = DIV_ROUND_UP(total, PAGE_SIZE);

	/* ---- выделяем контекст ---- */
	ctx = kmalloc(sizeof(*ctx), GFP_NOIO);
	if (!ctx)
		goto err_nomem;

	ctx->pages = kmalloc_array(nr_pages, sizeof(struct page *), GFP_NOIO);
	if (!ctx->pages) {
		kfree(ctx);
		goto err_nomem;
	}
	ctx->nr_pages = 0;
	ctx->orig_bio = orig_bio;

	/* ---- alloc_page по одной штуке ---- */
	for (i = 0; i < nr_pages; i++) {
		ctx->pages[i] = alloc_pages(GFP_NOIO, 1);
		if (!ctx->pages[i]) {
			ctx->nr_pages = i;
			goto err_free_pages;
		}
	}
	ctx->nr_pages = nr_pages;

	/* ---- для WRITE: скопировать данные в наши страницы ---- */
	if (op_is_write(bio_op(orig_bio)))
		bio_copy_to_pages(orig_bio, ctx->pages);

	/* ---- строим нормализованный bio ---- */
	/* nr_pages векторов, каждый: 1 страница, offset=0, len=PAGE_SIZE (или меньше) */
	new_bio = bio_alloc(pdev->backing_bdev, nr_pages,
			    orig_bio->bi_opf, GFP_NOIO);
	if (!new_bio)
		goto err_free_pages;

	new_bio->bi_iter.bi_sector = orig_bio->bi_iter.bi_sector;
	new_bio->bi_end_io         = proxy_endio;
	new_bio->bi_private        = ctx;

	{
		unsigned int rem = total;

		for (i = 0; i < nr_pages; i++) {
			unsigned int len = min_t(unsigned int, rem, PAGE_SIZE);

			if (bio_add_page(new_bio, ctx->pages[i], len, 0) != len) {
				/*
				 * Не должно происходить: мы запросили
				 * ровно nr_pages векторов при alloc.
				 */
				bio_put(new_bio);
				goto err_free_pages;
			}
			rem -= len;
		}
	}

	submit_bio_noacct(new_bio);
	return;

err_free_pages:
	for (i = 0; i < ctx->nr_pages; i++)
	__free_pages(ctx->pages[i], 1);
	kfree(ctx->pages);
	kfree(ctx);
err_nomem:
	orig_bio->bi_status = BLK_STS_RESOURCE;
	bio_endio(orig_bio);
}

static const struct block_device_operations proxy_bdev_ops = {
	.owner      = THIS_MODULE,
	.submit_bio = proxy_submit_bio,
};

/* ---------- создание / уничтожение устройства ---------- */

static int proxy_create(const char *path)
{
	struct proxy_dev    *pdev;
	struct file         *bdev_file;
	struct gendisk      *disk;
	struct queue_limits  lim = {
		.logical_block_size = 512,
		.max_hw_sectors     = PROXY_MAX_SECTORS,
	};
	int err;

	mutex_lock(&proxy_lock);

	if (g_pdev) {
		mutex_unlock(&proxy_lock);
		pr_err("blkproxy: устройство уже существует\n");
		return -EBUSY;
	}

	pdev = kzalloc(sizeof(*pdev), GFP_KERNEL);
	if (!pdev) {
		err = -ENOMEM;
		goto out_unlock;
	}

	/* Открываем backing-устройство (kernel 6.9+ API) */
	bdev_file = bdev_file_open_by_path(path,
					   BLK_OPEN_READ | BLK_OPEN_WRITE,
					   NULL, NULL);
	if (IS_ERR(bdev_file)) {
		err = PTR_ERR(bdev_file);
		pr_err("blkproxy: не удалось открыть %s: %d\n", path, err);
		goto out_free_pdev;
	}

	pdev->bdev_file    = bdev_file;
	pdev->backing_bdev = file_bdev(bdev_file);

	/* Выделяем gendisk вместе с очередью */
	disk = blk_alloc_disk(&lim, NUMA_NO_NODE);
	if (IS_ERR(disk)) {
		err = PTR_ERR(disk);
		goto out_close_bdev;
	}

	disk->major        = proxy_major;
	disk->first_minor  = 0;
	disk->minors       = 1;
	disk->fops         = &proxy_bdev_ops;
	disk->private_data = pdev;
	strscpy(disk->disk_name, PROXY_NAME, DISK_NAME_LEN);

	/* Ёмкость равна ёмкости backing-устройства (в 512-байт. секторах) */
	set_capacity(disk, bdev_nr_sectors(pdev->backing_bdev));

	err = add_disk(disk);
	if (err) {
		pr_err("blkproxy: add_disk вернул %d\n", err);
		goto out_put_disk;
	}

	pdev->disk = disk;
	g_pdev     = pdev;

	pr_info("blkproxy: /dev/%s создан поверх %s\n", PROXY_NAME, path);
	mutex_unlock(&proxy_lock);
	return 0;

out_put_disk:
	put_disk(disk);
out_close_bdev:
	fput(bdev_file);
out_free_pdev:
	kfree(pdev);
out_unlock:
	mutex_unlock(&proxy_lock);
	return err;
}

static void proxy_destroy(void)
{
	struct proxy_dev *pdev;

	mutex_lock(&proxy_lock);
	pdev   = g_pdev;
	g_pdev = NULL;
	mutex_unlock(&proxy_lock);

	if (!pdev)
		return;

	del_gendisk(pdev->disk);
	put_disk(pdev->disk);
	fput(pdev->bdev_file);
	kfree(pdev);

	pr_info("blkproxy: dev was destroyed\n");
}

/* ---------- sysfs-атрибуты ---------- */

/*
 * echo -n /dev/sdb > /sys/kernel/blkproxy/create
 */
static ssize_t create_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	char path[256];
	size_t len;

	len = min_t(size_t, count, sizeof(path) - 1);
	memcpy(path, buf, len);
	path[len] = '\0';

	/* echo добавляет '\n' — убираем */
	if (len && path[len - 1] == '\n')
		path[--len] = '\0';

	if (!len)
		return -EINVAL;

	return proxy_create(path) ?: (ssize_t)count;
}

/*
 * echo 1 > /sys/kernel/blkproxy/destroy
 */
static ssize_t destroy_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	proxy_destroy();
	return (ssize_t)count;
}

static struct kobj_attribute create_attr  = __ATTR_WO(create);
static struct kobj_attribute destroy_attr = __ATTR_WO(destroy);

static struct attribute *proxy_attrs[] = {
	&create_attr.attr,
	&destroy_attr.attr,
	NULL,
};

static const struct attribute_group proxy_attr_group = {
	.attrs = proxy_attrs,
};

/* ---------- init / exit ---------- */

static int __init proxy_init(void)
{
	int err;

	proxy_major = register_blkdev(0, PROXY_NAME);
	if (proxy_major < 0)
		return proxy_major;

	proxy_kobj = kobject_create_and_add(PROXY_NAME, kernel_kobj);
	if (!proxy_kobj) {
		err = -ENOMEM;
		goto err_unreg;
	}

	err = sysfs_create_group(proxy_kobj, &proxy_attr_group);
	if (err)
		goto err_kobj;

	pr_info("blkproxy: loaded, major=%d, "
		"sysfs=/sys/kernel/%s/\n", proxy_major, PROXY_NAME);
	return 0;

err_kobj:
	kobject_put(proxy_kobj);
err_unreg:
	unregister_blkdev(proxy_major, PROXY_NAME);
	return err;
}

static void __exit proxy_exit(void)
{
	proxy_destroy();
	sysfs_remove_group(proxy_kobj, &proxy_attr_group);
	kobject_put(proxy_kobj);
	unregister_blkdev(proxy_major, PROXY_NAME);
	pr_info("blkproxy: unloaded\n");
}

module_init(proxy_init);
module_exit(proxy_exit);
