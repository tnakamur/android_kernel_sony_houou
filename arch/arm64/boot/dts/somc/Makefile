ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_MACH_SONY_HOUOU_ROLA) += sdm630-ganges-houou_rola-overlay.dtbo
ifeq ($(CONFIG_MACH_SONY_HOUOU_ROLA),y)
sdm630-ganges-houou_rola-overlay.dtbo-base := ../qcom/sdm630.dtb
endif

else
dtb-$(CONFIG_MACH_SONY_HOUOU) += sdm630-ganges-houou.dtb
dtb-$(CONFIG_MACH_SONY_HOUOU_ROLA) += sdm630-ganges-houou_rola.dtb
endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtbo
