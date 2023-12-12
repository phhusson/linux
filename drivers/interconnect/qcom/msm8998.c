// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm MSM8998 Network-on-Chip (NoC) QoS driver
 * Copyright (c) 2020, AngeloGioacchino Del Regno
 *                     <angelogioacchino.delregno@somainline.org>
 * Copyright (C) 2020, Konrad Dybcio <konrad.dybcio@somainline.org>
 *
 */

#include <dt-bindings/interconnect/qcom,msm8998.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "icc-rpm.h"


enum {
        MSM8998_MASTER_IPA = 1,
        MSM8998_MASTER_CNOC_A2NOC,
        MSM8998_MASTER_SDCC_2,
        MSM8998_MASTER_SDCC_4,
        MSM8998_MASTER_BLSP_1,
        MSM8998_MASTER_BLSP_2,
        MSM8998_MASTER_UFS,
        MSM8998_MASTER_USB_HS,
        MSM8998_MASTER_USB3,
        MSM8998_MASTER_CRYPTO_C0,
        MSM8998_MASTER_GNOC_BIMC,
        MSM8998_MASTER_OXILI,
        MSM8998_MASTER_MNOC_BIMC,
        MSM8998_MASTER_SNOC_BIMC,
        MSM8998_MASTER_PIMEM,
        MSM8998_MASTER_SNOC_CNOC, //0x10
        MSM8998_MASTER_QDSS_DAP,
        MSM8998_MASTER_APPS_PROC,
        MSM8998_MASTER_CNOC_MNOC_MMSS_CFG,
        MSM8998_MASTER_CNOC_MNOC_CFG,
        MSM8998_MASTER_CPP,
        MSM8998_MASTER_JPEG,
        MSM8998_MASTER_MDP_P0,
        MSM8998_MASTER_MDP_P1,
        MSM8998_MASTER_VENUS,
        MSM8998_MASTER_VFE,
        MSM8998_MASTER_QDSS_ETR,
        MSM8998_MASTER_QDSS_BAM,
        MSM8998_MASTER_SNOC_CFG,
        MSM8998_MASTER_BIMC_SNOC,
        MSM8998_MASTER_A1NOC_SNOC,
        MSM8998_MASTER_A2NOC_SNOC,//0x20
        MSM8998_MASTER_GNOC_SNOC,
        MSM8998_MASTER_PCIE_0,
        MSM8998_MASTER_A2NOC_TSIF,
        MSM8998_MASTER_CRVIRT_A2NOC,
        MSM8998_MASTER_ROTATOR,
        MSM8998_MASTER_VENUS_VMEM,
        MSM8998_MASTER_HMSS,
        MSM8998_MASTER_BIMC_SNOC_0,
        MSM8998_MASTER_BIMC_SNOC_1,

        MSM8998_SLAVE_A1NOC_SNOC, //0x2a
        MSM8998_SLAVE_A2NOC_SNOC,
        MSM8998_SLAVE_EBI,
        MSM8998_SLAVE_HMSS_L3,
        MSM8998_SLAVE_CNOC_A2NOC,
        MSM8998_SLAVE_MPM,
        MSM8998_SLAVE_PMIC_ARB, // 0x30
        MSM8998_SLAVE_TLMM_NORTH,
        MSM8998_SLAVE_TCSR,
        MSM8998_SLAVE_PIMEM_CFG,
        MSM8998_SLAVE_IMEM_CFG,
        MSM8998_SLAVE_MESSAGE_RAM,
        MSM8998_SLAVE_GLM,
        MSM8998_SLAVE_BIMC_CFG,
        MSM8998_SLAVE_PRNG,
        MSM8998_SLAVE_SPDM,
        MSM8998_SLAVE_QDSS_CFG,
        MSM8998_SLAVE_CNOC_MNOC_CFG,
        MSM8998_SLAVE_SNOC_CFG,
        MSM8998_SLAVE_QM_CFG,
        MSM8998_SLAVE_CLK_CTL,
        MSM8998_SLAVE_MSS_CFG,
        MSM8998_SLAVE_UFS_CFG,
        MSM8998_SLAVE_A2NOC_CFG,
        MSM8998_SLAVE_A2NOC_SMMU_CFG,
        MSM8998_SLAVE_GPUSS_CFG,
        MSM8998_SLAVE_AHB2PHY,
        MSM8998_SLAVE_BLSP_1,
        MSM8998_SLAVE_SDCC_2,
        MSM8998_SLAVE_SDCC_4,
        MSM8998_SLAVE_BLSP_2,
        MSM8998_SLAVE_PDM,
        MSM8998_SLAVE_CNOC_MNOC_MMSS_CFG,
        MSM8998_SLAVE_USB_HS,
        MSM8998_SLAVE_USB3_0,
        MSM8998_SLAVE_SRVC_CNOC,
        MSM8998_SLAVE_GNOC_BIMC,
        MSM8998_SLAVE_GNOC_SNOC,
        MSM8998_SLAVE_CAMERA_CFG,
        MSM8998_SLAVE_CAMERA_THROTTLE_CFG,
        MSM8998_SLAVE_MISC_CFG,
        MSM8998_SLAVE_VENUS_THROTTLE_CFG,
        MSM8998_SLAVE_VENUS_CFG,
        MSM8998_SLAVE_MMSS_CLK_XPU_CFG,
        MSM8998_SLAVE_MMSS_CLK_CFG,
        MSM8998_SLAVE_MNOC_MPU_CFG,
        MSM8998_SLAVE_DISPLAY_CFG,
        MSM8998_SLAVE_CSI_PHY_CFG,
        MSM8998_SLAVE_DISPLAY_THROTTLE_CFG,
        MSM8998_SLAVE_SMMU_CFG,
        MSM8998_SLAVE_MNOC_BIMC,
        MSM8998_SLAVE_SRVC_MNOC,
        MSM8998_SLAVE_HMSS,
        MSM8998_SLAVE_LPASS,
        MSM8998_SLAVE_WLAN,
        MSM8998_SLAVE_CDSP,
        MSM8998_SLAVE_IPA,
        MSM8998_SLAVE_SNOC_BIMC,
        MSM8998_SLAVE_SNOC_CNOC,
        MSM8998_SLAVE_IMEM,
        MSM8998_SLAVE_PIMEM,
        MSM8998_SLAVE_QDSS_STM,
        MSM8998_SLAVE_SRVC_SNOC,
        MSM8998_SLAVE_BIMC_SNOC_0,
        MSM8998_SLAVE_BIMC_SNOC_1,
        MSM8998_SLAVE_SSC_CFG,
        MSM8998_SLAVE_SKL,
        MSM8998_SLAVE_TLMM_WEST,
        MSM8998_SLAVE_A1NOC_CFG,
        MSM8998_SLAVE_A1NOC_SMMU_CFG,
        MSM8998_SLAVE_TSIF,
        MSM8998_SLAVE_TLMM_EAST,
        MSM8998_SLAVE_CRVIRT_A2NOC,
        MSM8998_SLAVE_VMEM_CFG,
        MSM8998_SLAVE_VMEM,
        MSM8998_SLAVE_PCIE_0,
};

#define DEFINE_QNODE(_name, _id, _buswidth, _mas_rpm_id, _slv_rpm_id,   \
                     _ap_owned, _qos_mode, _qos_prio, _qos_port, ...)   \
static const u16 _name##_links[] = { \
__VA_ARGS__ \
}; \
                static struct qcom_icc_node _name = {                   \
                .name = #_name,                                         \
                .id = _id,                                              \
                .buswidth = _buswidth,                                  \
                .mas_rpm_id = _mas_rpm_id,                              \
                .slv_rpm_id = _slv_rpm_id,                              \
                .qos.ap_owned = _ap_owned,                              \
                .qos.qos_mode = _qos_mode,                              \
                .qos.areq_prio = _qos_prio,                             \
                .qos.prio_level = _qos_prio,                            \
                .qos.qos_port = _qos_port,                              \
                .num_links = COUNT_ARGS(__VA_ARGS__),      \
                .links = _name##_links, \
        }

DEFINE_QNODE(mas_pcie_0, MSM8998_MASTER_PCIE_0, 16, 45, -1, true, NOC_QOS_MODE_FIXED, 1, 1, MSM8998_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(mas_usb3, MSM8998_MASTER_USB3, 16, 32, -1, true, NOC_QOS_MODE_FIXED, 1, 2, MSM8998_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(mas_ufs, MSM8998_MASTER_UFS, 16, 68, -1, true, NOC_QOS_MODE_FIXED, 1, 2, MSM8998_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(mas_blsp_2, MSM8998_MASTER_BLSP_2, 16, 39, -1, false, NOC_QOS_MODE_FIXED, 0, 4, MSM8998_SLAVE_A1NOC_SNOC);
DEFINE_QNODE(mas_cnoc_a2noc, MSM8998_MASTER_CNOC_A2NOC, 8, 146, -1, true, -1, 0, -1, MSM8998_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(mas_ipa, MSM8998_MASTER_IPA, 8, 59, -1, true, NOC_QOS_MODE_FIXED, 1, 1, MSM8998_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(mas_sdcc_2, MSM8998_MASTER_SDCC_2, 8, 35, -1, false, NOC_QOS_MODE_FIXED, 0, 6, MSM8998_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(mas_sdcc_4, MSM8998_MASTER_SDCC_4, 8, 36, -1, false, NOC_QOS_MODE_FIXED, 0, 7, MSM8998_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(mas_tsif, MSM8998_MASTER_A2NOC_TSIF, 4, 37, -1, true, -1, 0, -1, MSM8998_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(mas_blsp_1, MSM8998_MASTER_BLSP_1, 16, 41, -1, false, NOC_QOS_MODE_FIXED, 0, 8, MSM8998_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(mas_crvirt_a2noc, MSM8998_MASTER_CRVIRT_A2NOC, 8, 145, -1, false, NOC_QOS_MODE_FIXED, 0, 9, MSM8998_SLAVE_A2NOC_SNOC);
DEFINE_QNODE(mas_gnoc_bimc, MSM8998_MASTER_GNOC_BIMC, 8, 144, -1, true, NOC_QOS_MODE_FIXED, 0, 0, MSM8998_SLAVE_EBI, MSM8998_SLAVE_BIMC_SNOC_0);
DEFINE_QNODE(mas_oxili, MSM8998_MASTER_OXILI, 8, 6, -1, true, NOC_QOS_MODE_BYPASS, 0, 1, MSM8998_SLAVE_BIMC_SNOC_1, MSM8998_SLAVE_HMSS_L3, MSM8998_SLAVE_EBI, MSM8998_SLAVE_BIMC_SNOC_0);
DEFINE_QNODE(mas_mnoc_bimc, MSM8998_MASTER_MNOC_BIMC, 8, 2, -1, true, NOC_QOS_MODE_BYPASS, 0, 2, MSM8998_SLAVE_BIMC_SNOC_1, MSM8998_SLAVE_HMSS_L3, MSM8998_SLAVE_EBI, MSM8998_SLAVE_BIMC_SNOC_0);
DEFINE_QNODE(mas_snoc_bimc, MSM8998_MASTER_SNOC_BIMC, 8, 3, -1, false, NOC_QOS_MODE_BYPASS, 0, 3, MSM8998_SLAVE_HMSS_L3, MSM8998_SLAVE_EBI);
DEFINE_QNODE(mas_snoc_cnoc, MSM8998_MASTER_SNOC_CNOC, 8, 52, -1, true, -1, 0, -1, MSM8998_SLAVE_SKL, MSM8998_SLAVE_BLSP_2, MSM8998_SLAVE_MESSAGE_RAM, MSM8998_SLAVE_TLMM_WEST, MSM8998_SLAVE_TSIF, MSM8998_SLAVE_MPM, MSM8998_SLAVE_BIMC_CFG, MSM8998_SLAVE_TLMM_EAST, MSM8998_SLAVE_SPDM, MSM8998_SLAVE_PIMEM_CFG, MSM8998_SLAVE_A1NOC_SMMU_CFG, MSM8998_SLAVE_BLSP_1, MSM8998_SLAVE_CLK_CTL, MSM8998_SLAVE_PRNG, MSM8998_SLAVE_USB3_0, MSM8998_SLAVE_QDSS_CFG, MSM8998_SLAVE_QM_CFG, MSM8998_SLAVE_A2NOC_CFG, MSM8998_SLAVE_PMIC_ARB, MSM8998_SLAVE_UFS_CFG, MSM8998_SLAVE_SRVC_CNOC, MSM8998_SLAVE_AHB2PHY, MSM8998_SLAVE_IPA, MSM8998_SLAVE_GLM, MSM8998_SLAVE_SNOC_CFG, MSM8998_SLAVE_SSC_CFG, MSM8998_SLAVE_SDCC_2, MSM8998_SLAVE_SDCC_4, MSM8998_SLAVE_PDM, MSM8998_SLAVE_CNOC_MNOC_MMSS_CFG, MSM8998_SLAVE_CNOC_MNOC_CFG, MSM8998_SLAVE_MSS_CFG, MSM8998_SLAVE_IMEM_CFG, MSM8998_SLAVE_A1NOC_CFG, MSM8998_SLAVE_GPUSS_CFG, MSM8998_SLAVE_TCSR, MSM8998_SLAVE_TLMM_NORTH);
DEFINE_QNODE(mas_qdss_dap, MSM8998_MASTER_QDSS_DAP, 8, 49, -1, true, -1, 0, -1, MSM8998_SLAVE_SKL, MSM8998_SLAVE_BLSP_2, MSM8998_SLAVE_MESSAGE_RAM, MSM8998_SLAVE_TLMM_WEST, MSM8998_SLAVE_TSIF, MSM8998_SLAVE_MPM, MSM8998_SLAVE_BIMC_CFG, MSM8998_SLAVE_TLMM_EAST, MSM8998_SLAVE_SPDM, MSM8998_SLAVE_PIMEM_CFG, MSM8998_SLAVE_A1NOC_SMMU_CFG, MSM8998_SLAVE_BLSP_1, MSM8998_SLAVE_CLK_CTL, MSM8998_SLAVE_PRNG, MSM8998_SLAVE_USB3_0, MSM8998_SLAVE_QDSS_CFG, MSM8998_SLAVE_QM_CFG, MSM8998_SLAVE_A2NOC_CFG, MSM8998_SLAVE_PMIC_ARB, MSM8998_SLAVE_UFS_CFG, MSM8998_SLAVE_SRVC_CNOC, MSM8998_SLAVE_AHB2PHY, MSM8998_SLAVE_IPA, MSM8998_SLAVE_GLM, MSM8998_SLAVE_SNOC_CFG, MSM8998_SLAVE_SDCC_2, MSM8998_SLAVE_SDCC_4, MSM8998_SLAVE_PDM, MSM8998_SLAVE_CNOC_MNOC_MMSS_CFG, MSM8998_SLAVE_CNOC_MNOC_CFG, MSM8998_SLAVE_MSS_CFG, MSM8998_SLAVE_IMEM_CFG, MSM8998_SLAVE_A1NOC_CFG, MSM8998_SLAVE_GPUSS_CFG, MSM8998_SLAVE_SSC_CFG, MSM8998_SLAVE_TCSR, MSM8998_SLAVE_TLMM_NORTH, MSM8998_SLAVE_CNOC_A2NOC);
DEFINE_QNODE(mas_crypto, MSM8998_MASTER_CRYPTO_C0, 650, 23, -1, false, -1, 0, -1, MSM8998_MASTER_CRVIRT_A2NOC);
DEFINE_QNODE(mas_apss_proc, MSM8998_MASTER_APPS_PROC, 32, 0, -1, true, -1, 0, -1, MSM8998_SLAVE_GNOC_BIMC);
DEFINE_QNODE(mas_cnoc_mnoc_mmss_cfg, MSM8998_MASTER_CNOC_MNOC_MMSS_CFG, 8, 4, -1, true, -1, 0, -1, MSM8998_SLAVE_CAMERA_THROTTLE_CFG, MSM8998_SLAVE_VENUS_CFG, MSM8998_SLAVE_MISC_CFG, MSM8998_SLAVE_CAMERA_CFG, MSM8998_SLAVE_DISPLAY_THROTTLE_CFG, MSM8998_SLAVE_VENUS_THROTTLE_CFG, MSM8998_SLAVE_DISPLAY_CFG, MSM8998_SLAVE_MMSS_CLK_CFG, MSM8998_SLAVE_VMEM_CFG, MSM8998_SLAVE_MMSS_CLK_XPU_CFG, MSM8998_SLAVE_SMMU_CFG);
DEFINE_QNODE(mas_cnoc_mnoc_cfg, MSM8998_MASTER_CNOC_MNOC_CFG, 8, 5, -1, true, -1, 0, -1, MSM8998_SLAVE_SRVC_MNOC);
DEFINE_QNODE(mas_cpp, MSM8998_MASTER_CPP, 32, 115, -1, true, NOC_QOS_MODE_BYPASS, 0, 5, MSM8998_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_jpeg, MSM8998_MASTER_JPEG, 32, 7, -1, true, NOC_QOS_MODE_BYPASS, 0, 7, MSM8998_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_mdp_p0, MSM8998_MASTER_MDP_P0, 32, 8, -1, true, NOC_QOS_MODE_BYPASS, 0, 1, MSM8998_SLAVE_MNOC_BIMC); /* vrail-comp???? */
DEFINE_QNODE(mas_mdp_p1, MSM8998_MASTER_MDP_P1, 32, 61, -1, true, NOC_QOS_MODE_BYPASS, 0, 2, MSM8998_SLAVE_MNOC_BIMC); /* vrail-comp??? */
DEFINE_QNODE(mas_rotator, MSM8998_MASTER_ROTATOR, 32, 120, -1, true, NOC_QOS_MODE_BYPASS, 0, 0, MSM8998_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_venus, MSM8998_MASTER_VENUS, 32, 9, -1, true, NOC_QOS_MODE_BYPASS, 0, 3, MSM8998_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_vfe, MSM8998_MASTER_VFE, 32, 11, -1, true, NOC_QOS_MODE_BYPASS, 0, 6, MSM8998_SLAVE_MNOC_BIMC);
DEFINE_QNODE(mas_venus_vmem, MSM8998_MASTER_VENUS_VMEM, 32, 121, -1, true, -1, 0, -1, MSM8998_SLAVE_VMEM);
DEFINE_QNODE(mas_hmss, MSM8998_MASTER_HMSS, 16, 118, -1, true, NOC_QOS_MODE_FIXED, 1, 3, MSM8998_SLAVE_PIMEM, MSM8998_SLAVE_IMEM, MSM8998_SLAVE_SNOC_BIMC);
DEFINE_QNODE(mas_qdss_bam, MSM8998_MASTER_QDSS_BAM, 16, 19, -1, true, NOC_QOS_MODE_FIXED, 1, 1, MSM8998_SLAVE_IMEM, MSM8998_SLAVE_PIMEM, MSM8998_SLAVE_SNOC_CNOC, MSM8998_SLAVE_SNOC_BIMC);
DEFINE_QNODE(mas_snoc_cfg, MSM8998_MASTER_SNOC_CFG, 16, 20, -1, false, -1, 0, -1, MSM8998_SLAVE_SRVC_SNOC);
DEFINE_QNODE(mas_bimc_snoc_0, MSM8998_MASTER_BIMC_SNOC_0, 16, 21, -1, false, -1, 0, -1, MSM8998_SLAVE_PIMEM, MSM8998_SLAVE_LPASS, MSM8998_SLAVE_HMSS, MSM8998_SLAVE_WLAN, MSM8998_SLAVE_SNOC_CNOC, MSM8998_SLAVE_IMEM, MSM8998_SLAVE_QDSS_STM);
DEFINE_QNODE(mas_bimc_snoc_1, MSM8998_MASTER_BIMC_SNOC_1, 16, 109, -1, true, -1, 0, -1, MSM8998_SLAVE_PCIE_0);
DEFINE_QNODE(mas_a1noc_snoc, MSM8998_MASTER_A1NOC_SNOC, 16, 111, -1, false, -1, 0, -1, MSM8998_SLAVE_PIMEM, MSM8998_SLAVE_PCIE_0, MSM8998_SLAVE_LPASS, MSM8998_SLAVE_HMSS, MSM8998_SLAVE_SNOC_BIMC, MSM8998_SLAVE_SNOC_CNOC, MSM8998_SLAVE_IMEM, MSM8998_SLAVE_QDSS_STM);
DEFINE_QNODE(mas_a2noc_snoc, MSM8998_MASTER_A2NOC_SNOC, 16, 112, -1, false, -1, 0, -1, MSM8998_SLAVE_PIMEM, MSM8998_SLAVE_PCIE_0, MSM8998_SLAVE_LPASS, MSM8998_SLAVE_HMSS, MSM8998_SLAVE_SNOC_BIMC, MSM8998_SLAVE_WLAN, MSM8998_SLAVE_SNOC_CNOC, MSM8998_SLAVE_IMEM, MSM8998_SLAVE_QDSS_STM);
DEFINE_QNODE(mas_qdss_etr, MSM8998_MASTER_QDSS_ETR, 16, 31, -1, true, NOC_QOS_MODE_FIXED, 1, 2, MSM8998_SLAVE_IMEM, MSM8998_MASTER_PIMEM, MSM8998_SLAVE_SNOC_CNOC, MSM8998_SLAVE_SNOC_BIMC);

/* slaves */
DEFINE_QNODE(slv_a1noc_snoc, MSM8998_SLAVE_A1NOC_SNOC, 16, -1, 142, false, -1, 0, -1, MSM8998_MASTER_A1NOC_SNOC);
DEFINE_QNODE(slv_a2noc_snoc, MSM8998_SLAVE_A2NOC_SNOC, 16, -1, 143, false, -1, 0, -1, MSM8998_MASTER_A2NOC_SNOC);
DEFINE_QNODE(slv_ebi, MSM8998_SLAVE_EBI, 8, -1, 0, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_hmss_l3, MSM8998_SLAVE_HMSS_L3, 8, -1, 160, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_bimc_snoc_0, MSM8998_SLAVE_BIMC_SNOC_0, 8, -1, 2, false, -1, 0, -1, MSM8998_MASTER_BIMC_SNOC_0);
DEFINE_QNODE(slv_bimc_snoc_1, MSM8998_SLAVE_BIMC_SNOC_1, 8, -1, 138, true, -1, 0, -1, MSM8998_MASTER_BIMC_SNOC_1);
DEFINE_QNODE(slv_cnoc_a2noc, MSM8998_SLAVE_CNOC_A2NOC, 4, -1, 208, true, -1, 0, -1, MSM8998_MASTER_CNOC_A2NOC);
DEFINE_QNODE(slv_ssc_cfg, MSM8998_SLAVE_SSC_CFG, 4, -1, 177, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_mpm, MSM8998_SLAVE_MPM, 4, -1, 62, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pmic_arb, MSM8998_SLAVE_PMIC_ARB, 4, -1, 59, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_tlmm_north, MSM8998_SLAVE_TLMM_NORTH, 4, -1, 214, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pimem_cfg, MSM8998_SLAVE_PIMEM_CFG, 4, -1, 167, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_imem_cfg, MSM8998_SLAVE_IMEM_CFG, 4, -1, 54, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_message_ram, MSM8998_SLAVE_MESSAGE_RAM, 4, -1, 55, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_skl, MSM8998_SLAVE_SKL, 4, -1, 196, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_bimc_cfg, MSM8998_SLAVE_BIMC_CFG, 4, -1, 56, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_prng, MSM8998_SLAVE_PRNG, 4, -1, 44, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a2noc_cfg, MSM8998_SLAVE_A2NOC_CFG, 4, -1, 150, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_ipa, MSM8998_SLAVE_IPA, 4, -1, 183, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_tcsr, MSM8998_SLAVE_TCSR, 4, -1, 50, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_snoc_cfg, MSM8998_SLAVE_SNOC_CFG, 4, -1, 70, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_clk_ctl, MSM8998_SLAVE_CLK_CTL, 4, -1, 47, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_glm, MSM8998_SLAVE_GLM, 4, -1, 209, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_spdm, MSM8998_SLAVE_SPDM, 4, -1, 60, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_gpuss_cfg, MSM8998_SLAVE_GPUSS_CFG, 4, -1, 11, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_cnoc_mnoc_cfg, MSM8998_SLAVE_CNOC_MNOC_CFG, 4, -1, 66, true, -1, 0, -1, MSM8998_MASTER_CNOC_MNOC_CFG);
DEFINE_QNODE(slv_qm_cfg, MSM8998_SLAVE_QM_CFG, 4, -1, 212, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_mss_cfg, MSM8998_SLAVE_MSS_CFG, 4, -1, 48, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_ufs_cfg, MSM8998_SLAVE_UFS_CFG, 4, -1, 92, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_tlmm_west, MSM8998_SLAVE_TLMM_WEST, 4, -1, 215, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a1noc_cfg, MSM8998_SLAVE_A1NOC_CFG, 4, -1, 147, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_ahb2phy, MSM8998_SLAVE_AHB2PHY, 4, -1, 163, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_blsp_2, MSM8998_SLAVE_BLSP_2, 4, -1, 37, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_pdm, MSM8998_SLAVE_PDM, 4, -1, 41, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_usb3_0, MSM8998_SLAVE_USB3_0, 4, -1, 22, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_a1noc_smmu_cfg, MSM8998_SLAVE_A1NOC_SMMU_CFG, 8, -1, 149, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_blsp_1, MSM8998_SLAVE_BLSP_1, 4, -1, 39, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_sdcc_2, MSM8998_SLAVE_SDCC_2, 4, -1, 33, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_sdcc_4, MSM8998_SLAVE_SDCC_4, 4, -1, 34, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_tsif, MSM8998_SLAVE_TSIF, 4, -1, 35, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_qdss_cfg, MSM8998_SLAVE_QDSS_CFG, 4, -1, 63, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_tlmm_east, MSM8998_SLAVE_TLMM_EAST, 4, -1, 213, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_cnoc_mnoc_mmss_cfg, MSM8998_SLAVE_CNOC_MNOC_MMSS_CFG, 8, -1, 58, true, -1, 0, -1, MSM8998_MASTER_CNOC_MNOC_MMSS_CFG);
DEFINE_QNODE(slv_srvc_cnoc, MSM8998_SLAVE_SRVC_CNOC, 4, -1, 76, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_crvirt_a2noc, MSM8998_SLAVE_CRVIRT_A2NOC, 8, -1, 207, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_gnoc_bimc, MSM8998_SLAVE_GNOC_BIMC, 32, -1, 210, true, -1, 0, -1, MSM8998_MASTER_GNOC_BIMC);
DEFINE_QNODE(slv_camera_cfg, MSM8998_SLAVE_CAMERA_CFG, 8, -1, 3, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_camera_throttle_cfg, MSM8998_SLAVE_CAMERA_THROTTLE_CFG, 8, -1, 154, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_misc_cfg, MSM8998_SLAVE_MISC_CFG, 8, -1, 8, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_venus_throttle_cfg, MSM8998_SLAVE_VENUS_THROTTLE_CFG, 8, -1, 178, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_venus_cfg, MSM8998_SLAVE_VENUS_CFG, 8, -1, 10, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_vmem_cfg, MSM8998_SLAVE_VMEM_CFG, 8, -1, 180, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_mmss_clk_xpu_cfg, MSM8998_SLAVE_MMSS_CLK_XPU_CFG, 8, -1, 13, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_mmss_clk_cfg, MSM8998_SLAVE_MMSS_CLK_CFG, 8, -1, 12, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_display_cfg, MSM8998_SLAVE_DISPLAY_CFG, 8, -1, 4, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_display_throttle_cfg, MSM8998_SLAVE_DISPLAY_THROTTLE_CFG, 4, -1, 156, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_smmu_cfg, MSM8998_SLAVE_SMMU_CFG, 8, -1, 205, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_mnoc_bimc, MSM8998_SLAVE_MNOC_BIMC, 32, -1, 16, true, -1, 0, -1, MSM8998_MASTER_MNOC_BIMC);
DEFINE_QNODE(slv_vmem, MSM8998_SLAVE_VMEM, 32, -1, 179, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_srvc_mnoc, MSM8998_SLAVE_SRVC_MNOC, 8, -1, 17, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_hmss, MSM8998_SLAVE_HMSS, 16, -1, 20, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_lpass, MSM8998_SLAVE_LPASS, 16, -1, 21, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_wlan, MSM8998_SLAVE_WLAN, 16, -1, 206, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_snoc_bimc, MSM8998_SLAVE_SNOC_BIMC, 32, -1, 24, false, -1, 0, -1, MSM8998_MASTER_SNOC_BIMC);
DEFINE_QNODE(slv_snoc_cnoc, MSM8998_SLAVE_SNOC_CNOC, 16, -1, 25, false, -1, 0, -1, MSM8998_MASTER_SNOC_CNOC);
DEFINE_QNODE(slv_imem, MSM8998_SLAVE_IMEM, 16, -1, 26, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_pimem, MSM8998_SLAVE_PIMEM, 16, -1, 166, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_qdss_stm, MSM8998_SLAVE_QDSS_STM, 16, -1, 30, false, -1, 0, -1, 0);
DEFINE_QNODE(slv_pcie_0, MSM8998_SLAVE_PCIE_0, 16, -1, 84, true, -1, 0, -1, 0);
DEFINE_QNODE(slv_srvc_snoc, MSM8998_SLAVE_SRVC_SNOC, 16, -1, 29, false, -1, 0, -1, 0);

static struct qcom_icc_node *msm8998_a1noc_nodes[] = {
	[MASTER_PCIE_0] = &mas_pcie_0,
	[MASTER_USB3] = &mas_usb3,
	[MASTER_UFS] = &mas_ufs,
	[MASTER_BLSP_2] = &mas_blsp_2,
	[SLAVE_A1NOC_SNOC] = &slv_a1noc_snoc,
};

static const struct regmap_config msm8998_a1noc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x60000,
	.fast_io	= true,
};

static struct qcom_icc_desc msm8998_a1noc = {
    .type = QCOM_ICC_NOC,
	.nodes = msm8998_a1noc_nodes,
    .bus_clk_desc = &aggre1_clk,
	.num_nodes = ARRAY_SIZE(msm8998_a1noc_nodes),
	.regmap_cfg = &msm8998_a1noc_regmap_config,
};

static struct qcom_icc_node *msm8998_a2noc_nodes[] = {
	[MASTER_IPA] = &mas_ipa,
	[MASTER_CNOC_A2NOC] = &mas_cnoc_a2noc,
	[MASTER_SDCC_2] = &mas_sdcc_2,
	[MASTER_SDCC_4] = &mas_sdcc_4,
	[MASTER_TSIF] = &mas_tsif,
	[MASTER_BLSP_1] = &mas_blsp_1,
	[MASTER_CRVIRT_A2NOC] = &mas_crvirt_a2noc,
	[MASTER_CRYPTO_C0] = &mas_crypto,
	[SLAVE_A2NOC_SNOC] = &slv_a2noc_snoc,
	[SLAVE_CRVIRT_A2NOC] = &slv_crvirt_a2noc,
};

static const struct regmap_config msm8998_a2noc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x60000,
	.fast_io	= true,
};

static struct qcom_icc_desc msm8998_a2noc = {
    .type = QCOM_ICC_NOC,
	.nodes = msm8998_a2noc_nodes,
    .bus_clk_desc = &aggre2_clk,
	.num_nodes = ARRAY_SIZE(msm8998_a2noc_nodes),
	.regmap_cfg = &msm8998_a2noc_regmap_config,
};

static struct qcom_icc_node *msm8998_bimc_nodes[] = {
	[MASTER_GNOC_BIMC] = &mas_gnoc_bimc,
	[MASTER_OXILI] = &mas_oxili,
	[MASTER_MNOC_BIMC] = &mas_mnoc_bimc,
	[MASTER_SNOC_BIMC] = &mas_snoc_bimc,
	[SLAVE_EBI] = &slv_ebi,
	[SLAVE_HMSS_L3] = &slv_hmss_l3,
	[SLAVE_BIMC_SNOC_0] = &slv_bimc_snoc_0,
	[SLAVE_BIMC_SNOC_1] = &slv_bimc_snoc_1,
};

static const struct regmap_config msm8998_bimc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x80000,
	.fast_io	= true,
};

static struct qcom_icc_desc msm8998_bimc = {
    .type = QCOM_ICC_BIMC,
	.nodes = msm8998_bimc_nodes,
	.num_nodes = ARRAY_SIZE(msm8998_bimc_nodes),
    .bus_clk_desc = &bimc_clk,
	.regmap_cfg = &msm8998_bimc_regmap_config,
};

static struct qcom_icc_node *msm8998_cnoc_nodes[] = {
	[MASTER_SNOC_CNOC] = &mas_snoc_cnoc,
	[MASTER_QDSS_DAP] = &mas_qdss_dap,
	[SLAVE_CNOC_A2NOC] = &slv_cnoc_a2noc,
	[SLAVE_SSC_CFG] = &slv_ssc_cfg,
	[SLAVE_MPM] = &slv_mpm,
	[SLAVE_PMIC_ARB] = &slv_pmic_arb,
	[SLAVE_TLMM_NORTH] = &slv_tlmm_north,
	[SLAVE_PIMEM_CFG] = &slv_pimem_cfg,
	[SLAVE_IMEM_CFG] = &slv_imem_cfg,
	[SLAVE_MESSAGE_RAM] = &slv_message_ram,
	[SLAVE_SKL] = &slv_skl,
	[SLAVE_BIMC_CFG] = &slv_bimc_cfg,
	[SLAVE_PRNG] = &slv_prng,
	[SLAVE_A2NOC_CFG] = &slv_a2noc_cfg,
	[SLAVE_IPA] = &slv_ipa,
	[SLAVE_TCSR] = &slv_tcsr,
	[SLAVE_SNOC_CFG] = &slv_snoc_cfg,
	[SLAVE_CLK_CTL] = &slv_clk_ctl,
	[SLAVE_GLM] = &slv_glm,
	[SLAVE_SPDM] = &slv_spdm,
	[SLAVE_GPUSS_CFG] = &slv_gpuss_cfg,
	[SLAVE_CNOC_MNOC_CFG] = &slv_cnoc_mnoc_cfg,
	[SLAVE_QM_CFG] = &slv_qm_cfg,
	[SLAVE_MSS_CFG] = &slv_mss_cfg,
	[SLAVE_UFS_CFG] = &slv_ufs_cfg,
	[SLAVE_TLMM_WEST] = &slv_tlmm_west,
	[SLAVE_A1NOC_CFG] = &slv_a1noc_cfg,
	[SLAVE_AHB2PHY] = &slv_ahb2phy,
	[SLAVE_BLSP_2] = &slv_blsp_2,
	[SLAVE_PDM] = &slv_pdm,
	[SLAVE_USB3_0] = &slv_usb3_0,
	[SLAVE_A1NOC_SMMU_CFG] = &slv_a1noc_smmu_cfg,
	[SLAVE_BLSP_1] = &slv_blsp_1,
	[SLAVE_SDCC_2] = &slv_sdcc_2,
	[SLAVE_SDCC_4] = &slv_sdcc_4,
	[SLAVE_TSIF] = &slv_tsif,
	[SLAVE_QDSS_CFG] = &slv_qdss_cfg,
	[SLAVE_TLMM_EAST] = &slv_tlmm_east,
	[SLAVE_CNOC_MNOC_MMSS_CFG] = &slv_cnoc_mnoc_mmss_cfg,
	[SLAVE_SRVC_CNOC] = &slv_srvc_cnoc,
};

static const struct regmap_config msm8998_cnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x10000,
	.fast_io	= true,
};

static struct qcom_icc_desc msm8998_cnoc = {
    .type = QCOM_ICC_NOC,
	.nodes = msm8998_cnoc_nodes,
    .bus_clk_desc = &bus_2_clk,
	.num_nodes = ARRAY_SIZE(msm8998_cnoc_nodes),
	.regmap_cfg = &msm8998_cnoc_regmap_config,
};

static struct qcom_icc_node *msm8998_gnoc_nodes[] = {
	[MASTER_APSS_PROC] = &mas_apss_proc,
	[SLAVE_GNOC_BIMC] = &slv_gnoc_bimc,
};

static const struct regmap_config msm8998_gnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x10000,
	.fast_io	= true,
};

static struct qcom_icc_desc msm8998_gnoc = {
    .type = QCOM_ICC_NOC,
	.nodes = msm8998_gnoc_nodes,
	.num_nodes = ARRAY_SIZE(msm8998_gnoc_nodes),
	.regmap_cfg = &msm8998_gnoc_regmap_config,
};

static struct qcom_icc_node *msm8998_mnoc_nodes[] = {
	[MASTER_CNOC_MNOC_CFG] = &mas_cnoc_mnoc_cfg,
	[MASTER_CPP] = &mas_cpp,
	[MASTER_JPEG] = &mas_jpeg,
	[MASTER_MDP_P0] = &mas_mdp_p0,
	[MASTER_MDP_P1] = &mas_mdp_p1,
	[MASTER_ROTATOR] = &mas_rotator,
	[MASTER_VENUS] = &mas_venus,
	[MASTER_VFE] = &mas_vfe,
	[MASTER_VENUS_VMEM] = &mas_venus_vmem,
	[SLAVE_MNOC_BIMC] = &slv_mnoc_bimc,
	[SLAVE_VMEM] = &slv_vmem,
	[SLAVE_SRVC_MNOC] = &slv_srvc_mnoc,
	[MASTER_CNOC_MNOC_MMSS_CFG] = &mas_cnoc_mnoc_mmss_cfg,
	[SLAVE_CAMERA_CFG] = &slv_camera_cfg,
	[SLAVE_CAMERA_THROTTLE_CFG] = &slv_camera_throttle_cfg,
	[SLAVE_MISC_CFG] = &slv_misc_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &slv_venus_throttle_cfg,
	[SLAVE_VENUS_CFG] = &slv_venus_cfg,
	[SLAVE_VMEM_CFG] = &slv_vmem_cfg,
	[SLAVE_MMSS_CLK_XPU_CFG] = &slv_mmss_clk_xpu_cfg,
	[SLAVE_MMSS_CLK_CFG] = &slv_mmss_clk_cfg,
	[SLAVE_DISPLAY_CFG] = &slv_display_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &slv_display_throttle_cfg,
	[SLAVE_SMMU_CFG] = &slv_smmu_cfg,
};

static const struct regmap_config msm8998_mnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x10000,
	.fast_io	= true,
};

static struct qcom_icc_desc msm8998_mnoc = {
    .type = QCOM_ICC_NOC,
	.nodes = msm8998_mnoc_nodes,
    .bus_clk_desc = &mmaxi_0_clk,
	.num_nodes = ARRAY_SIZE(msm8998_mnoc_nodes),
	.regmap_cfg = &msm8998_mnoc_regmap_config,
};

static struct qcom_icc_node *msm8998_snoc_nodes[] = {
	[MASTER_HMSS] = &mas_hmss,
	[MASTER_QDSS_BAM] = &mas_qdss_bam,
	[MASTER_SNOC_CFG] = &mas_snoc_cfg,
	[MASTER_BIMC_SNOC_0] = &mas_bimc_snoc_0,
	[MASTER_BIMC_SNOC_1] = &mas_bimc_snoc_1,
	[MASTER_A1NOC_SNOC] = &mas_a1noc_snoc,
	[MASTER_A2NOC_SNOC] = &mas_a2noc_snoc,
	[MASTER_QDSS_ETR] = &mas_qdss_etr,
	[SLAVE_HMSS] = &slv_hmss,
	[SLAVE_LPASS] = &slv_lpass,
	[SLAVE_WLAN] = &slv_wlan,
	[SLAVE_SNOC_BIMC] = &slv_snoc_bimc,
	[SLAVE_SNOC_CNOC] = &slv_snoc_cnoc,
	[SLAVE_IMEM] = &slv_imem,
	[SLAVE_PIMEM] = &slv_pimem,
	[SLAVE_QDSS_STM] = &slv_qdss_stm,
	[SLAVE_PCIE_0] = &slv_pcie_0,
	[SLAVE_SRVC_SNOC] = &slv_srvc_snoc,
};

static const struct regmap_config msm8998_snoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x40000,
	.fast_io	= true,
};

static struct qcom_icc_desc msm8998_snoc = {
    .type = QCOM_ICC_NOC,
	.nodes = msm8998_snoc_nodes,
    .bus_clk_desc = &bus_1_clk,
	.num_nodes = ARRAY_SIZE(msm8998_snoc_nodes),
	.regmap_cfg = &msm8998_snoc_regmap_config,
};

static const struct of_device_id msm8998_noc_of_match[] = {
	{ .compatible = "qcom,msm8998-a1noc", .data = &msm8998_a1noc },
	{ .compatible = "qcom,msm8998-a2noc", .data = &msm8998_a2noc },
	{ .compatible = "qcom,msm8998-bimc", .data = &msm8998_bimc },
	{ .compatible = "qcom,msm8998-cnoc", .data = &msm8998_cnoc },
	{ .compatible = "qcom,msm8998-gnoc", .data = &msm8998_gnoc },
	{ .compatible = "qcom,msm8998-mnoc", .data = &msm8998_mnoc },
	{ .compatible = "qcom,msm8998-snoc", .data = &msm8998_snoc },
	{ },
};
MODULE_DEVICE_TABLE(of, msm8998_noc_of_match);

static struct platform_driver msm8998_noc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-msm8998",
		.of_match_table = msm8998_noc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(msm8998_noc_driver);
MODULE_DESCRIPTION("Qualcomm msm8998 NoC driver");
MODULE_LICENSE("GPL v2");
