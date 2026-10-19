/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Tomeu Vizoso
 */
#ifndef __DRM_UAPI_ROCKET_ACCEL_H__
#define __DRM_UAPI_ROCKET_ACCEL_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_ROCKET_CREATE_BO			0x00
#define DRM_ROCKET_SUBMIT			0x01
#define DRM_ROCKET_PREP_BO			0x02
#define DRM_ROCKET_FINI_BO			0x03
#define DRM_ROCKET_IMPORT_BO			0x04

#define DRM_IOCTL_ROCKET_CREATE_BO		DRM_IOWR(DRM_COMMAND_BASE + DRM_ROCKET_CREATE_BO, struct drm_rocket_create_bo)
#define DRM_IOCTL_ROCKET_SUBMIT			DRM_IOW(DRM_COMMAND_BASE + DRM_ROCKET_SUBMIT, struct drm_rocket_submit)
#define DRM_IOCTL_ROCKET_PREP_BO		DRM_IOW(DRM_COMMAND_BASE + DRM_ROCKET_PREP_BO, struct drm_rocket_prep_bo)
#define DRM_IOCTL_ROCKET_FINI_BO		DRM_IOW(DRM_COMMAND_BASE + DRM_ROCKET_FINI_BO, struct drm_rocket_fini_bo)
#define DRM_IOCTL_ROCKET_IMPORT_BO		DRM_IOWR(DRM_COMMAND_BASE + DRM_ROCKET_IMPORT_BO, struct drm_rocket_import_bo)

/**
 * struct drm_rocket_create_bo - ioctl argument for creating Rocket BOs.
 *
 */
struct drm_rocket_create_bo {
	/**
	 * @size: Input: Size of the requested BO.
	 */
	__u32 size;

	/**
	 * @handle: Output: GEM handle for the BO.
	 */
	__u32 handle;

	/**
	 * @dma_address: Output: DMA address for the BO in the NPU address
	 * space.  This address is private to the DRM fd and is valid for
	 * the lifetime of the GEM handle.
	 */
	__u64 dma_address;

	/**
	 * @offset: Output: Offset into the drm node to use for subsequent
	 * mmap call.
	 */
	__u64 offset;
};

/**
 * struct drm_rocket_import_bo - ioctl argument for importing a DMA-BUF as a
 * Rocket BO.
 *
 * The DMA-BUF (typically allocated from a dma-heap) is imported and mapped
 * into the NPU address space of the DRM file that issues the ioctl. The
 * returned @dma_address is only valid for the lifetime of the GEM handle.
 */
struct drm_rocket_import_bo {
	/**
	 * @fd: Input: DMA-BUF file descriptor to import.
	 */
	__s32 fd;

	/**
	 * @handle: Output: GEM handle for the BO.
	 */
	__u32 handle;

	/**
	 * @dma_address: Output: DMA address for the BO in the NPU address
	 * space.  This address is private to the DRM fd and is valid for
	 * the lifetime of the GEM handle.
	 */
	__u64 dma_address;

	/**
	 * @offset: Output: Offset into the drm node to use for subsequent
	 * mmap call.
	 */
	__u64 offset;
};

/**
 * struct drm_rocket_prep_bo - ioctl argument for starting CPU ownership of the BO.
 *
 * Takes care of waiting for any NPU jobs that might still use the NPU and performs cache
 * synchronization.
 */
struct drm_rocket_prep_bo {
	/**
	 * @handle: Input: GEM handle of the buffer object.
	 */
	__u32 handle;

	/**
	 * @reserved: Reserved, must be zero.
	 */
	__u32 reserved;

	/**
	 * @timeout_ns: Input: Amount of time to wait for NPU jobs.
	 */
	__s64 timeout_ns;
};

/**
 * struct drm_rocket_fini_bo - ioctl argument for finishing CPU ownership of the BO.
 *
 * Synchronize caches for NPU access.
 */
struct drm_rocket_fini_bo {
	/**
	 * @handle: Input: GEM handle of the buffer object.
	 */
	__u32 handle;

	/**
	 * @reserved: Reserved, must be zero.
	 */
	__u32 reserved;
};

/**
 * struct drm_rocket_task - A task to be run on the NPU
 *
 * A task is the smallest unit of work that can be run on the NPU.
 */
struct drm_rocket_task {
	/**
	 * @regcmd: Input: DMA address to NPU mapping of register command buffer
	 */
	__u32 regcmd;

	/**
	 * @regcmd_count: Input: Number of commands in the register command
	 * buffer
	 */
	__u32 regcmd_count;
};

/**
 * struct drm_rocket_job - A job to be run on the NPU
 *
 * The kernel will schedule the execution of this job taking into account its
 * dependencies with other jobs. All tasks in the same job will be executed
 * sequentially on the same core, to benefit from memory residency in SRAM.
 */
struct drm_rocket_job {
	/**
	 * @tasks: Input: Pointer to an array of struct drm_rocket_task.
	 */
	__u64 tasks;

	/**
	 * @in_bo_handles: Input: Pointer to a u32 array of the BOs that
	 * are read by the job.
	 */
	__u64 in_bo_handles;

	/**
	 * @out_bo_handles: Input: Pointer to a u32 array of the BOs that
	 * are written to by the job.
	 */
	__u64 out_bo_handles;

	/**
	 * @task_count: Input: Number of tasks passed in.
	 */
	__u32 task_count;

	/**
	 * @task_struct_size: Input: Size in bytes of the structs in the
	 * @tasks field.
	 */
	__u32 task_struct_size;

	/**
	 * @in_bo_handle_count: Input: Number of input BO handles passed in
	 * (size is that times 4).
	 */
	__u32 in_bo_handle_count;

	/**
	 * @out_bo_handle_count: Input: Number of output BO handles passed in
	 * (size is that times 4).
	 */
	__u32 out_bo_handle_count;

	/**
	 * @flags: Input: Combination of DRM_ROCKET_JOB_* flags, used to
	 * enable explicit synchronization for this job.
	 *
	 * The @flags, @in_fence_fd and @out_fence_fd fields were added
	 * after the original job UAPI. They are only honored if
	 * job_struct_size (in the parent &struct drm_rocket_submit) covers
	 * them; otherwise implicit synchronization through the buffer
	 * reservations is used.
	 */
	__u32 flags;

	/**
	 * @in_fence_fd: Input: sync_file file descriptor for a fence that
	 * must be signaled before this job is submitted, or -1 for no
	 * explicit dependency. Only used when @flags contains
	 * %DRM_ROCKET_JOB_IN_FENCE_FD.
	 */
	__s32 in_fence_fd;

	/**
	 * @out_fence_fd: Output: sync_file file descriptor for a fence that
	 * will be signaled when this job completes, or -1 if no explicit
	 * output fence was requested. Only written when @flags contains
	 * %DRM_ROCKET_JOB_OUT_FENCE_FD.
	 */
	__s32 out_fence_fd;
};

/**
 * DRM_ROCKET_JOB_IN_FENCE_FD - wait for @drm_rocket_job.in_fence_fd
 * before running the job.
 */
#define DRM_ROCKET_JOB_IN_FENCE_FD	(1 << 0)

/**
 * DRM_ROCKET_JOB_OUT_FENCE_FD - return a fence in
 * @drm_rocket_job.out_fence_fd that is signaled when the job completes.
 */
#define DRM_ROCKET_JOB_OUT_FENCE_FD	(1 << 1)

/**
 * struct drm_rocket_submit - ioctl argument for submitting commands to the NPU.
 *
 * The kernel will schedule the execution of these jobs in dependency order.
 */
struct drm_rocket_submit {
	/**
	 * @jobs: Input: Pointer to an array of struct drm_rocket_job.
	 */
	__u64 jobs;

	/**
	 * @job_count: Input: Number of jobs passed in.
	 */
	__u32 job_count;

	/**
	 * @job_struct_size: Input: Size in bytes of the structs in the
	 * @jobs field.
	 */
	__u32 job_struct_size;

	/**
	 * @reserved: Reserved, must be zero.
	 */
	__u64 reserved;
};

#if defined(__cplusplus)
}
#endif

#endif /* __DRM_UAPI_ROCKET_ACCEL_H__ */
