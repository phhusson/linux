obj-y += initsnd_dev.o
obj-y += io.o idle.o irq.o timer.o dma.o memory.o
obj-y += devices.o
obj-$(CONFIG_MSM_AMSS_VERSION_5200) += proc_comm_wince.o
obj-$(CONFIG_MSM_AMSS_VERSION_6120) += proc_comm_wince.o
obj-$(CONFIG_MSM_AMSS_VERSION_6125) += proc_comm_wince.o
obj-$(CONFIG_MSM_AMSS_VERSION_6150) += proc_comm_wince.o
obj-$(CONFIG_MSM_AMSS_VERSION_6210) += proc_comm.o
obj-$(CONFIG_MSM_AMSS_VERSION_6220) += proc_comm.o
obj-$(CONFIG_MSM_AMSS_VERSION_6225) += proc_comm.o
obj-y += vreg.o
obj-y += acpuclock.o
#obj-y += clock.o
obj-y += clock-wince.o
obj-y += gpio.o generic_gpio.o
obj-y += nand_partitions.o
obj-y += htc_hw.o smem_log.o

obj-$(CONFIG_MSM_FIQ_SUPPORT) += fiq_glue.o
obj-$(CONFIG_MACH_TROUT) += board-trout-rfkill.o
obj-$(CONFIG_MSM_SMD) += smd.o smd_tty.o
# smd_qmi.o

obj-$(CONFIG_MSM_SMD_6150) += smd_6150.o
obj-$(CONFIG_MSM_ONCRPCROUTER) += smd_rpcrouter.o
obj-$(CONFIG_MSM_ONCRPCROUTER) += smd_rpcrouter_device.o
obj-$(CONFIG_MSM_ONCRPCROUTER) += smd_rpcrouter_servers.o
#obj-$(CONFIG_MSM_RPCSERVERS) += rpc_server_dog_keepalive.o
obj-$(CONFIG_MSM_RPCSERVERS) += rpc_server_time_remote.o
obj-$(CONFIG_MSM_ADSP) += qdsp5/
obj-$(CONFIG_MSM_HW3D_DONUT) += hw3d_donut.o
obj-$(CONFIG_MSM_HW3D) += hw3d.o
obj-$(CONFIG_PM) += pm.o
obj-$(CONFIG_MSM_CPU_FREQ) += cpufreq.o
 
obj-$(CONFIG_MACH_HTCRAPHAEL) += board-htcraphael.o board-htcraphael-navi.o board-htcraphael-panel.o htc_wifi_nvs.o mmc.o msm_vibrator.o msm-gpio-keys.o pmem.o htc_acoustic.o
obj-$(CONFIG_MACH_HTCRAPHAEL_CDMA) += board-htcraphael.o board-htcraphael-navi.o board-htcraphael-panel.o htc_wifi_nvs.o mmc.o msm_vibrator.o  msm-gpio-keys.o pmem.o htc_acoustic.o
obj-$(CONFIG_MACH_HTCRAPHAEL_CDMA500) += board-htcraphael.o board-htcraphael-navi.o board-htcraphael-panel.o htc_wifi_nvs.o mmc.o msm_vibrator.o  msm-gpio-keys.o pmem.o htc_acoustic.o
obj-$(CONFIG_MACH_HTCDIAMOND) += board-htcdiamond.o board-htcraphael-navi.o board-htcraphael-panel.o htc_wifi_nvs.o mmc.o msm_vibrator.o board-htcdiamond-panel.o msm-gpio-keys.o pmem.o htc_acoustic.o
obj-$(CONFIG_MACH_HTCDIAMOND_CDMA) += board-htcdiamond.o board-htcraphael-navi.o board-htcraphael-panel.o htc_wifi_nvs.o mmc.o msm_vibrator.o msm-gpio-keys.o pmem.o htc_acoustic.o

obj-$(CONFIG_MACH_HTCBLACKSTONE) += board-htcblackstone.o board-htcblackstone-panel.o htc_wifi_nvs.o mmc.o  pmem.o
obj-$(CONFIG_MACH_HTCBLACKSTONE) += msm_vibrator.o msm-gpio-keys.o htc_acoustic.o

obj-$(CONFIG_MACH_HTCKOVSKY) += board-htckovsky.o board-htcblackstone-panel.o htc_wifi_nvs.o mmc.o pmem.o
obj-$(CONFIG_MACH_HTCVENUS) += msm_vibrator.o msm-gpio-keys.o htc_acoustic.o
obj-$(CONFIG_MACH_HTCTOPAZ) += board-htctopaz.o board-htctopaz-panel.o board-htctopaz-keypad.o htc_wifi_nvs.o mmc.o pmem.o msm_vibrator.o msm-gpio-keys.o htc_acoustic.o
obj-$(CONFIG_MACH_HTCRHODIUM) += board-htcrhodium.o board-htctopaz-panel.o board-htcrhodium-keypad.o htc_wifi_nvs.o mmc.o pmem.o msm_vibrator.o htc_acoustic.o msm-gpio-keys.o board-htcrhodium-audio.o
obj-$(CONFIG_MACH_HTCRHODIUM) += board-htcrhodium-proximity.o


obj-$(CONFIG_MACH_HALIBUT) += board-halibut.o board-halibut-keypad.o
obj-$(CONFIG_MACH_TROUT) += board-trout.o board-trout-gpio.o
obj-$(CONFIG_MACH_TROUT) += board-trout-keypad.o board-trout-panel.o
obj-$(CONFIG_MACH_TROUT) += htc_akm_cal.o htc_wifi_nvs.o 
obj-$(CONFIG_MACH_TROUT) += board-trout-mmc.o board-trout-wifi.o
obj-$(CONFIG_MACH_TROUT) += devices_htc.o
obj-$(CONFIG_MACH_SAPPHIRE) += board-sapphire.o board-sapphire-gpio.o
obj-$(CONFIG_MACH_SAPPHIRE) += board-sapphire-keypad.o board-sapphire-panel.o
obj-$(CONFIG_MACH_SAPPHIRE) += board-sapphire-mmc.o board-sapphire-wifi.o
obj-$(CONFIG_MACH_SAPPHIRE) += board-sapphire-rfkill.o msm_vibrator.o
obj-$(CONFIG_MACH_SAPPHIRE) += devices_htc.o

obj-$(CONFIG_TROUT_BATTCHG) += htc_battery.o

obj-$(CONFIG_HTC_PWRSINK) += htc_pwrsink.o pwrsink.o
obj-$(CONFIG_HTC_HEADSET) += htc_headset.o
obj-$(CONFIG_MSM_SMEM_BATTCHG) += htc_battery_smem.o
obj-$(CONFIG_RFKILL) += board-htcraphael-rfkill.o

obj-$(CONFIG_HTC_FB_CONSOLE) += htc_fb_console.o
obj-$(CONFIG_TOUCHSCREEN_TSSC_MANAGER)       += htc_tp_cal.o

