// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <drm/drm_device.h>
#include <drm/drm_prime.h>
#include <drm/drm_print.h>
#include <drm/drm_utils.h>
#include <drm/rocket_accel.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>

#include "rocket_drv.h"
#include "rocket_gem.h"

MODULE_IMPORT_NS("DMA_BUF");

static int rocket_dma_buf_begin_cpu_access(struct dma_buf *dma_buf,
					 enum dma_data_direction direction)
{
	struct drm_gem_object *obj = dma_buf->priv;
	struct drm_gem_shmem_object *shmem = &to_rocket_bo(obj)->base;

	dma_sync_sgtable_for_cpu(obj->dev->dev, shmem->sgt, direction);
	return 0;
}

static int rocket_dma_buf_end_cpu_access(struct dma_buf *dma_buf,
				       enum dma_data_direction direction)
{
	struct drm_gem_object *obj = dma_buf->priv;
	struct drm_gem_shmem_object *shmem = &to_rocket_bo(obj)->base;

	dma_sync_sgtable_for_device(obj->dev->dev, shmem->sgt, direction);
	return 0;
}

static const struct dma_buf_ops rocket_dma_buf_ops = {
	.attach = drm_gem_map_attach,
	.detach = drm_gem_map_detach,
	.map_dma_buf = drm_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
	.release = drm_gem_dmabuf_release,
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
	.begin_cpu_access = rocket_dma_buf_begin_cpu_access,
	.end_cpu_access = rocket_dma_buf_end_cpu_access,
};

static struct dma_buf *rocket_gem_export(struct drm_gem_object *obj, int flags)
{
	struct dma_buf_export_info info = {
		.exp_name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
		.ops = &rocket_dma_buf_ops,
		.size = obj->size,
		.flags = flags,
		.priv = obj,
		.resv = obj->resv,
	};

	/*
	 * Cached native mappings need exporter cache-maintenance callbacks.
	 * Our own dma-buf ops also avoid PRIME's self-import shortcut: each
	 * DRM file needs its own GEM object and IOVA for an imported buffer.
	 */
	return drm_gem_dmabuf_export(obj->dev, &info);
}

static void rocket_gem_bo_free(struct drm_gem_object *obj)
{
	struct rocket_gem_object *bo = to_rocket_bo(obj);

	drm_WARN_ON(obj->dev, refcount_read(&bo->base.pages_use_count) > 1);

	/*
	 * Buffers that were never mapped into the NPU address space (for
	 * example those created by the generic PRIME import path) have no
	 * domain attached and only need their backing storage released.
	 */
	if (bo->domain) {
		struct rocket_iommu_domain *domain = bo->domain;
		size_t unmapped;

		unmapped = iommu_unmap(bo->domain->domain, bo->mm.start, bo->size);
		drm_WARN_ON(obj->dev, unmapped != bo->size);

		mutex_lock(&domain->mm_lock);
		drm_mm_remove_node(&bo->mm);
		mutex_unlock(&domain->mm_lock);

		rocket_iommu_domain_put(bo->domain);
		bo->domain = NULL;
	}

	drm_gem_shmem_free(&bo->base);
}

static const struct drm_gem_object_funcs rocket_gem_funcs = {
	.free = rocket_gem_bo_free,
	.export = rocket_gem_export,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = drm_gem_shmem_object_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = drm_gem_shmem_object_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = drm_gem_shmem_object_mmap,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

struct drm_gem_object *rocket_gem_create_object(struct drm_device *dev, size_t size)
{
	struct rocket_gem_object *obj;

	obj = kzalloc_obj(*obj);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	obj->base.base.funcs = &rocket_gem_funcs;

	return &obj->base.base;
}

static int rocket_gem_object_map(struct drm_device *dev,
				 struct rocket_file_priv *rocket_priv,
				 struct rocket_gem_object *rkt_obj,
				 struct sg_table *sgt)
{
	struct rocket_iommu_domain *domain = rocket_priv->domain;
	ssize_t mapped;
	int ret = 0;

	mutex_lock(&domain->mm_lock);
	if (rkt_obj->domain) {
		if (rkt_obj->domain != domain)
			ret = -EINVAL;
		goto out_unlock;
	}

	rkt_obj->size = rkt_obj->base.base.size;
	ret = drm_mm_insert_node_generic(&domain->mm, &rkt_obj->mm,
					 rkt_obj->size, PAGE_SIZE,
					 0, 0);
	if (ret)
		goto out_unlock;

	mapped = iommu_map_sgtable(domain->domain,
				   rkt_obj->mm.start, sgt,
				   IOMMU_READ | IOMMU_WRITE);
	if (mapped < 0 || mapped != rkt_obj->size) {
		drm_err(dev, "failed to map buffer: size=%zd request_size=%zu\n",
			mapped, rkt_obj->size);
		ret = mapped < 0 ? (int)mapped : -ENOMEM;
		if (mapped > 0)
			iommu_unmap(domain->domain, rkt_obj->mm.start, mapped);
		goto err_remove_node;
	}

	rkt_obj->domain = rocket_iommu_domain_get(rocket_priv);
	goto out_unlock;

err_remove_node:
	drm_mm_remove_node(&rkt_obj->mm);
out_unlock:
	mutex_unlock(&domain->mm_lock);
	return ret;
}

int rocket_ioctl_create_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rocket_file_priv *rocket_priv = file->driver_priv;
	struct drm_rocket_create_bo *args = data;
	struct drm_gem_shmem_object *shmem_obj;
	struct rocket_gem_object *rkt_obj;
	struct drm_gem_object *gem_obj;
	struct sg_table *sgt;
	int ret;

	shmem_obj = drm_gem_shmem_create(dev, args->size);
	if (IS_ERR(shmem_obj))
		return PTR_ERR(shmem_obj);

	gem_obj = &shmem_obj->base;
	rkt_obj = to_rocket_bo(gem_obj);

	rkt_obj->domain = NULL;
	rkt_obj->size = args->size;
	rkt_obj->offset = 0;

	sgt = drm_gem_shmem_get_pages_sgt(shmem_obj);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_put;
	}

	ret = rocket_gem_object_map(dev, rocket_priv, rkt_obj, sgt);
	if (ret)
		goto err_put;

	args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);
	args->dma_address = rkt_obj->mm.start;

	ret = drm_gem_handle_create(file, gem_obj, &args->handle);
	if (ret)
		goto err_put;

	drm_gem_object_put(gem_obj);

	return 0;

err_put:
	drm_gem_object_put(gem_obj);

	return ret;
}

int rocket_ioctl_import_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rocket_file_priv *rocket_priv = file->driver_priv;
	struct drm_rocket_import_bo *args = data;
	struct drm_gem_shmem_object *shmem_obj;
	struct rocket_gem_object *rkt_obj;
	struct drm_gem_object *gem_obj;
	int ret;

	if (args->fd < 0)
		return -EINVAL;

	/*
	 * Use the PRIME handle path so obj->dma_buf and the per-file cache
	 * are initialized, including the reference required by GEM mmap.
	 */
	ret = drm_gem_prime_fd_to_handle(dev, file, args->fd, &args->handle);
	if (ret)
		return ret;

	gem_obj = drm_gem_object_lookup(file, args->handle);
	if (!gem_obj)
		return -ENOENT;

	shmem_obj = &to_rocket_bo(gem_obj)->base;
	rkt_obj = to_rocket_bo(gem_obj);

	ret = rocket_gem_object_map(dev, rocket_priv, rkt_obj, shmem_obj->sgt);
	if (ret)
		goto err_handle;

	args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);
	args->dma_address = rkt_obj->mm.start;

	drm_gem_object_put(gem_obj);

	return 0;

err_handle:
	drm_gem_handle_delete(file, args->handle);
	drm_gem_object_put(gem_obj);

	return ret;
}

int rocket_ioctl_prep_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_rocket_prep_bo *args = data;
	unsigned long timeout = drm_timeout_abs_to_jiffies(args->timeout_ns);
	struct drm_gem_object *gem_obj;
	struct drm_gem_shmem_object *shmem_obj;
	long ret = 0;

	if (args->reserved != 0) {
		drm_dbg(dev, "Reserved field in drm_rocket_prep_bo struct should be 0.\n");
		return -EINVAL;
	}

	gem_obj = drm_gem_object_lookup(file, args->handle);
	if (!gem_obj)
		return -ENOENT;

	ret = dma_resv_wait_timeout(gem_obj->resv, DMA_RESV_USAGE_READ, true, timeout);
	if (!ret)
		ret = timeout ? -ETIMEDOUT : -EBUSY;
	else if (ret > 0)
		ret = 0;
	if (ret)
		goto out_put;

	shmem_obj = &to_rocket_bo(gem_obj)->base;

	if (gem_obj->import_attach)
		ret = dma_buf_begin_cpu_access(gem_obj->import_attach->dmabuf,
					     DMA_BIDIRECTIONAL);
	else
		dma_sync_sgtable_for_cpu(dev->dev, shmem_obj->sgt, DMA_BIDIRECTIONAL);

out_put:
	drm_gem_object_put(gem_obj);
	return ret;
}

int rocket_ioctl_fini_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_rocket_fini_bo *args = data;
	struct drm_gem_shmem_object *shmem_obj;
	struct rocket_gem_object *rkt_obj;
	struct drm_gem_object *gem_obj;
	int ret = 0;

	if (args->reserved != 0) {
		drm_dbg(dev, "Reserved field in drm_rocket_fini_bo struct should be 0.\n");
		return -EINVAL;
	}

	gem_obj = drm_gem_object_lookup(file, args->handle);
	if (!gem_obj)
		return -ENOENT;

	rkt_obj = to_rocket_bo(gem_obj);
	shmem_obj = &rkt_obj->base;

	if (gem_obj->import_attach)
		ret = dma_buf_end_cpu_access(gem_obj->import_attach->dmabuf,
					   DMA_BIDIRECTIONAL);
	else
		dma_sync_sgtable_for_device(dev->dev, shmem_obj->sgt, DMA_BIDIRECTIONAL);

	drm_gem_object_put(gem_obj);

	return ret;
}
