/* arch/arm/mach-msm/qdsp5/adsp_6120.h
 *
 * Copyright (c) 2008 QUALCOMM Incorporated.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "adsp.h"

/* Firmware modules */
typedef enum { 
	QDSP_MODULE_KERNEL=0x0,		// 0x0
	QDSP_MODULE_AFETASK=0x1,		// 0x1
	QDSP_MODULE_AUDPLAY0TASK=0x2,	// 0x2+
	QDSP_MODULE_AUDPPTASK=0x3,		// 0x3+
	QDSP_MODULE_VIDEOTASK=0x4,
	QDSP_MODULE_AUDRECTASK=0x1e, // ??
	QDSP_MODULE_AUDPREPROCTASK=0x1f,
	QDSP_MODULE_VOCENCTASK=0x9,
	QDSP_MODULE_VOCDECTASK=0xa,
	QDSP_MODULE_VOICEPROCTASK=0xb,
	QDSP_MODULE_LPMTASK=0x2c, // ??
	QDSP_MODULE_QCAMTASK=0x2d, // ??
	QDSP_MODULE_VIDEOENCTASK=0x26, // ??
	QDSP_MODULE_VFETASK=0x27, // ??
	QDSP_MODULE_JPEGTASK=0x2b,

//	=0x2b,
//	=0x2c,
	QDSP_MODULE_MAX=0x33,		// 0x2e
} qdsp_module_type;

#define QDSP_RTOS_MAX_TASK_ID  19U

/* Table of modules indexed by task ID for the image0 */
static qdsp_module_type qdsp_image0_task_to_module_table[] = {
	QDSP_MODULE_KERNEL,		// 0x0  0
	QDSP_MODULE_AFETASK,		// 0x1  1
	QDSP_MODULE_VOCDECTASK,		// 0x13 a
	QDSP_MODULE_VOCENCTASK,		// 0x12 9
	QDSP_MODULE_VIDEOTASK,		// 0x5 4
	QDSP_MODULE_MAX,		// 0x15 33
	QDSP_MODULE_VOICEPROCTASK,	// 0x14 b
	QDSP_MODULE_MAX,		// 0x16 33
	QDSP_MODULE_MAX,		// 0x1f 33
	QDSP_MODULE_AUDPPTASK,		// 0x4 3
	QDSP_MODULE_AUDPLAY0TASK,	// 0x2 2
	QDSP_MODULE_MAX,		// 0x3 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	0x12,		// 0x2a 12
	QDSP_MODULE_MAX			// 0x2a 33
};

/* Queue offset table indexed by queue ID for the image0 */
static uint32_t qdsp_image0_queue_offset_table[] = {
	0x65a,               /* QDSP_lpmCommandQueue              */
	0x5f2,               /* QDSP_mpuAfeQueue                  */
	QDSP_RTOS_NO_QUEUE,               /* QDSP_mpuGraphicsCmdQueue          */ 
	0x606,               /* QDSP_mpuModmathCmdQueue           */
	0x61e,               /* QDSP_mpuVDecCmdQueue              */
	0x622,               /* QDSP_mpuVDecPktQueue              */
	0x61a,               /* QDSP_mpuVEncCmdQueue              */
	0x5fa,               /* QDSP_rxMpuDecCmdQueue             */
	0x5fe,               /* QDSP_rxMpuDecPktQueue             */
	0x602,               /* QDSP_txMpuEncQueue                */
	0x626,               /* QDSP_uPAudPPCmd1Queue             */
	0x62a,               /* QDSP_uPAudPPCmd2Queue             */
	0x62e,               /* QDSP_uPAudPPCmd3Queue             */
	0x63e,               /* QDSP_uPAudPlay0BitStreamCtrlQueue */
	0x642,               /* QDSP_uPAudPlay1BitStreamCtrlQueue */
	0x646,               /* QDSP_uPAudPlay2BitStreamCtrlQueue */
	0x64a,               /* QDSP_uPAudPlay3BitStreamCtrlQueue */
	0x64e,               /* QDSP_uPAudPlay4BitStreamCtrlQueue */
	0x632,               /* QDSP_uPAudPreProcCmdQueue         */
	0x63a,               /* QDSP_uPAudRecBitStreamQueue       */
	0x636,               /* QDSP_uPJpegActionCmdQueue         */
	0x656,               /* QDSP_uPJpegCfgCmdQueue            */
	0x652,               /* QDSP_uPAudRecCmdQueue             */
	0x5f6,               /* QDSP_uPVocProcQueue               */
	0x60e,               /* QDSP_vfeCommandQueue              */
	0x616,               /* QDSP_vfeCommandScaleQueue         */
	0x612,               /* QDSP_vfeCommandTableQueue         */
        0x60a                /* QDSP_uPDiagQueue */
};

/* Table of modules indexed by task ID for the COMBO image */
static qdsp_module_type qdsp_combo_task_to_module_table[] = {
	QDSP_MODULE_KERNEL,		// 0x0  0
	QDSP_MODULE_AFETASK,		// 0x1  1
	QDSP_MODULE_VOCDECTASK,		// 0x13 a
	QDSP_MODULE_VOCENCTASK,		// 0x12 9
	QDSP_MODULE_VIDEOTASK,		// 0x5 4
	QDSP_MODULE_VIDEOENCTASK,		// 0x15 26
	QDSP_MODULE_VOICEPROCTASK,	// 0x14 b
	QDSP_MODULE_VFETASK,		// 0x16 27
	0x2b,		// 0x1f 2b
	QDSP_MODULE_AUDPPTASK,		// 0x4 3
	QDSP_MODULE_AUDPLAY0TASK,	// 0x2 2
	0x14,
	0x15,
	0x16,
	0x17,
	0x2c,
	0x1e,
	0x1f,
	0x2e,
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	0x12,
	QDSP_MODULE_MAX			// 0x2a 33
};

/* Queue offset table indexed by queue ID for the COMBO image */
static uint32_t qdsp_combo_queue_offset_table[] = {
	0x65a,               /* QDSP_lpmCommandQueue              */
	0x5f2,               /* QDSP_mpuAfeQueue                  */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuGraphicsCmdQueue          */ 
	0x606,               /* QDSP_mpuModmathCmdQueue           */
	0x61e,               /* QDSP_mpuVDecCmdQueue              */
	0x622,               /* QDSP_mpuVDecPktQueue              */
	0x61a,               /* QDSP_mpuVEncCmdQueue              */
	0x5fa,               /* QDSP_rxMpuDecCmdQueue             */
	0x5fe,               /* QDSP_rxMpuDecPktQueue             */
	0x602,               /* QDSP_txMpuEncQueue                */
	0x626,               /* QDSP_uPAudPPCmd1Queue             */
	0x62a,               /* QDSP_uPAudPPCmd2Queue             */
	0x62e,               /* QDSP_uPAudPPCmd3Queue             */
	0x63e,               /* QDSP_uPAudPlay0BitStreamCtrlQueue */
	0x642,               /* QDSP_uPAudPlay1BitStreamCtrlQueue */
	0x646,               /* QDSP_uPAudPlay2BitStreamCtrlQueue */
	0x64a,               /* QDSP_uPAudPlay3BitStreamCtrlQueue */
	0x64e,               /* QDSP_uPAudPlay4BitStreamCtrlQueue */
	0x632,               /* QDSP_uPAudPreProcCmdQueue         */
	0x63a,               /* QDSP_uPAudRecBitStreamQueue       */
	0x636,               /* QDSP_uPJpegActionCmdQueue         */
	0x656,               /* QDSP_uPJpegCfgCmdQueue            */
	0x652,               /* QDSP_uPAudRecCmdQueue             */
	0x5f6,               /* QDSP_uPVocProcQueue               */
	0x60e,               /* QDSP_vfeCommandQueue              */
	0x616,               /* QDSP_vfeCommandScaleQueue         */
	0x612,               /* QDSP_vfeCommandTableQueue         */
        0x60a                /* QDSP_uPDiagQueue */
};

/* Table of modules indexed by task ID for the GAUDIO image */
static qdsp_module_type qdsp_gaudio_task_to_module_table[] = {
	QDSP_MODULE_KERNEL,		// 0x0  0
	QDSP_MODULE_AFETASK,		// 0x1  1
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_VIDEOTASK,		// 0x2a 4
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_AUDPPTASK,		// 0x4 3
	QDSP_MODULE_AUDPLAY0TASK,	// 0x2 2
	QDSP_MODULE_MAX,	// 0x3  33
	QDSP_MODULE_MAX,	// 0x23 33
	QDSP_MODULE_MAX,	// 0x24 33
	QDSP_MODULE_MAX,	// 0x25 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0xd 33
	QDSP_MODULE_MAX,	// 0xe 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,	// 0x26 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX			// 0x2a 33
};

/* Queue offset table indexed by queue ID for the GAUDIO image */
static uint32_t qdsp_gaudio_queue_offset_table[] = {
	QDSP_RTOS_NO_QUEUE,  /* QDSP_lpmCommandQueue              */
	0x391,               /* QDSP_mpuAfeQueue                  */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuGraphicsCmdQueue          */
	0x395,  /* QDSP_mpuModmathCmdQueue           */ 
	0x399,  /* QDSP_mpuVDecCmdQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuVDecPktQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuVEncCmdQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_rxMpuDecCmdQueue             */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_rxMpuDecPktQueue             */
	0x39d,  /* QDSP_txMpuEncQueue                */
	0x3a1,               /* QDSP_uPAudPPCmd1Queue             */
	0x3a5,               /* QDSP_uPAudPPCmd2Queue             */
	0x3a9,               /* QDSP_uPAudPPCmd3Queue             */
	QDSP_RTOS_NO_QUEUE,               /* QDSP_uPAudPlay0BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,               /* QDSP_uPAudPlay1BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,               /* QDSP_uPAudPlay2BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,               /* QDSP_uPAudPlay3BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,               /* QDSP_uPAudPlay4BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,               /* QDSP_uPAudPreProcCmdQueue         */
	QDSP_RTOS_NO_QUEUE,               /* QDSP_uPAudRecBitStreamQueue       */
	QDSP_RTOS_NO_QUEUE,               /* QDSP_uPAudRecCmdQueue             */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPJpegActionCmdQueue         */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPJpegCfgCmdQueue            */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPVocProcQueue               */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_vfeCommandQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_vfeCommandScaleQueue         */
	QDSP_RTOS_NO_QUEUE   /* QDSP_vfeCommandTableQueue         */
};

/* Table of modules indexed by task ID for the QTV_LP image */
static qdsp_module_type qdsp_qtv_lp_task_to_module_table[] = {
	QDSP_MODULE_KERNEL,		// 0x0 0
	QDSP_MODULE_AFETASK,		// 0x1 1
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_VIDEOTASK,		// 0x5 4
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_AUDPPTASK,		// 0x4 3
	QDSP_MODULE_AUDPLAY0TASK,	// 0x2 2
	QDSP_MODULE_MAX,		// 0x2a
	QDSP_MODULE_MAX,		// 0x2a
	QDSP_MODULE_MAX,		// 0x2a
	QDSP_MODULE_MAX,		// 0x2a
	QDSP_MODULE_MAX,		// 0x2a
	QDSP_MODULE_MAX,		// 0x2a
	QDSP_MODULE_MAX,		// 0x2a
	QDSP_MODULE_MAX,		// 0x2a
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
};

/* Queue offset table indexed by queue ID for the QTV_LP image */
static uint32_t qdsp_qtv_lp_queue_offset_table[] = {
	QDSP_RTOS_NO_QUEUE,  /* QDSP_lpmCommandQueue              */
	0x3ce,               /* QDSP_mpuAfeQueue                  */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuGraphicsCmdQueue          */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuModmathCmdQueue           */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuVDecCmdQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuVDecPktQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_mpuVEncCmdQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_rxMpuDecCmdQueue             */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_rxMpuDecPktQueue             */
	0x3d2,               /* QDSP_txMpuEncQueue                */
	0x3d6,               /* QDSP_uPAudPPCmd1Queue             */
	0x3da,               /* QDSP_uPAudPPCmd2Queue             */
	0x3de,               /* QDSP_uPAudPPCmd3Queue             */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay0BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay1BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay2BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay3BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPlay4BitStreamCtrlQueue */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudPreProcCmdQueue         */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudRecBitStreamQueue       */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPAudRecCmdQueue             */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPJpegActionCmdQueue         */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPJpegCfgCmdQueue            */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_uPVocProcQueue               */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_vfeCommandQueue              */
	QDSP_RTOS_NO_QUEUE,  /* QDSP_vfeCommandScaleQueue         */
	QDSP_RTOS_NO_QUEUE   /* QDSP_vfeCommandTableQueue         */
};

/* Table of modules indexed by task ID for the image4 */
static qdsp_module_type qdsp_image4_task_to_module_table[] = {
	QDSP_MODULE_KERNEL,		// 0
	QDSP_MODULE_AFETASK,		// 1
	QDSP_MODULE_MAX,		// 33
	QDSP_MODULE_MAX,		// 33
	QDSP_MODULE_MAX,		// 33
	QDSP_MODULE_MAX,		// 33
	QDSP_MODULE_MAX,		// 33
	QDSP_MODULE_MAX,		// 33
	QDSP_MODULE_MAX,		// 33
	QDSP_MODULE_AUDPPTASK,		// 3
	QDSP_MODULE_AUDPLAY0TASK,	// 2
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
};

/* Queue offset table indexed by queue ID for the image 4*/
static uint32_t qdsp_image4_queue_offset_table[] = {
	QDSP_RTOS_NO_QUEUE,
	0x3ce,             
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	0x3d2,             
	0x3d6,             
	0x3da,             
	0x3de,             
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
};

/* Table of modules indexed by task ID for the image5 */
static qdsp_module_type qdsp_image5_task_to_module_table[] = {
	QDSP_MODULE_KERNEL,		// 0
	QDSP_MODULE_AFETASK,		// 1
	QDSP_MODULE_VOCDECTASK,		// a
	QDSP_MODULE_VOCENCTASK,		// 9
	QDSP_MODULE_MAX,		// 33
	QDSP_MODULE_MAX,		// 33
	QDSP_MODULE_VOICEPROCTASK,	// b
	QDSP_MODULE_MAX,		// 33
	QDSP_MODULE_MAX,		// 33
	QDSP_MODULE_AUDPPTASK,		// 3
	QDSP_MODULE_MAX,	// 
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,	//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		//
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
	QDSP_MODULE_MAX,		// 0x2a 33
};

/* Queue offset table indexed by queue ID for the image 5*/
static uint32_t qdsp_image5_queue_offset_table[] = {
	QDSP_RTOS_NO_QUEUE,
	0x57a,             
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	0x582,             
	0x586,             
	0x58a,             
	0x58e,            
	0x592,             
	0x596,             
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	0x57e, 
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
	QDSP_RTOS_NO_QUEUE,
};

/* Tables to convert tasks to modules */
static uint32_t *qdsp_task_to_module[] = {
	qdsp_image0_task_to_module_table,
	qdsp_combo_task_to_module_table,
	qdsp_gaudio_task_to_module_table,
	qdsp_qtv_lp_task_to_module_table,
	qdsp_image4_task_to_module_table,
	qdsp_image5_task_to_module_table,
};

/* Tables to retrieve queue offsets */
static uint32_t *qdsp_queue_offset_table[] = {
	qdsp_image0_queue_offset_table,
	qdsp_combo_queue_offset_table,
	qdsp_gaudio_queue_offset_table,
	qdsp_qtv_lp_queue_offset_table,
	qdsp_image4_queue_offset_table,
	qdsp_image5_queue_offset_table,
};

#define QDSP_MODULE(n) \
	{ .name = #n, .pdev_name = "adsp_" #n, .id = QDSP_MODULE_##n }

static struct adsp_module_info module_info[] = {
	QDSP_MODULE(AUDPLAY0TASK),
	QDSP_MODULE(AUDPPTASK),
	QDSP_MODULE(AUDRECTASK),
	QDSP_MODULE(AUDPREPROCTASK),
	QDSP_MODULE(VFETASK),
	QDSP_MODULE(QCAMTASK),
	QDSP_MODULE(LPMTASK),
	QDSP_MODULE(JPEGTASK), 
	QDSP_MODULE(VIDEOTASK),
/*	QDSP_MODULE(VDEC_LP_MODE),	*/
};

int adsp_init_info_6125(struct adsp_info *info)
{
	info->send_irq =   0x00c00200;
	info->read_ctrl =  0x00400038;
	info->write_ctrl = 0x00400034;

	info->max_msg16_size = 193;
	info->max_msg32_size = 8;

	info->max_task_id = QDSP_RTOS_MAX_TASK_ID;
	info->max_module_id = QDSP_MODULE_MAX - 1;
	info->max_queue_id = QDSP_QUEUE_MAX;
	info->max_image_id = 6;
	info->queue_offset = qdsp_queue_offset_table;
	info->task_to_module = qdsp_task_to_module;

	info->module_count = ARRAY_SIZE(module_info);
	info->module = module_info;
	return 0;
}
