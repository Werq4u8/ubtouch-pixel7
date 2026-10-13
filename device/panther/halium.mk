#
# Copyright (C) 2023-2024 The Halium Project
#
# Halium 13 Product Configuration for Google Pixel 7 (panther)
# Inherits from LineageOS 20 pantah/panther device trees.
#

# Inherit from LineageOS device base
$(call inherit-product, device/google/panther/lineage_panther.mk)

# Product metadata
PRODUCT_NAME := halium_panther
PRODUCT_DEVICE := panther
PRODUCT_BRAND := google
PRODUCT_MODEL := Pixel 7
PRODUCT_MANUFACTURER := Google

# Halium architecture & versioning
BOARD_HALIUM_VERSION := 13.0
TARGET_USES_HALIUM := true
BOARD_USES_HALIUM := true
BOARD_USES_HYBRIS := true

# Tensor G2 (gs201) Graphics & Display Architecture
# GPU: Arm Mali-G710 MP7 (Valhall v9 architecture)
# Gralloc: Android 13 Gralloc 4.0 / AIDL Graphics Mapper
LIBHYBRIS_USE_HW_ALLOCATOR := true
LIBHYBRIS_USE_GRALLOC_4 := true
BOARD_USES_DRM_HWCOMPOSER := false
TARGET_USES_HWC2 := true

# Audio & Sensor HALs: Pixel 7 uses Android 13 AIDL HAL services
TARGET_USES_AIDL_AUDIO := true
TARGET_USES_AIDL_SENSORS := true
TARGET_USES_AIDL_LIGHT := true
TARGET_USES_AIDL_POWER := true

# Pixel 7 Partition & Boot configuration
# Pantah uses GKI 2.0 with separate boot.img, init_boot.img, and vendor_boot.img
TARGET_BOOTIMAGE_USE_EXT2 := true
BOARD_BUILD_SYSTEM_ROOT_IMAGE := false
BOARD_USES_RECOVERY_AS_BOOT := false
PRODUCT_USE_DYNAMIC_PARTITIONS := true

# Halium rootfs & Android container filesystem config
PRODUCT_PACKAGES += \
    init.halium.rc \
    strace \
    gdb \
    libhybris \
    libhybris-tests \
    libandroid-properties \
    libui \
    libgui \
    libbinder \
    libbinder_ndk \
    android.hardware.graphics.allocator@4.0 \
    android.hardware.graphics.mapper@4.0 \
    ubtouch-radio-bridge \
    ubtouch-camera-bridge \
    ubtouch-audio-bridge \
    ubtouch-gnss-bridge

# Install Halium init script and interactive USB loader hook to ramdisk and system
PRODUCT_COPY_FILES += \
    device/google/panther/init.halium.rc:$(TARGET_COPY_OUT_RAMDISK)/init.halium.rc \
    device/google/panther/init.halium.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.halium.rc \
    device/google/panther/usb-insert-hook.sh:$(TARGET_COPY_OUT_RAMDISK)/system/bin/usb-insert-hook.sh \
    device/google/panther/usb-insert-hook.sh:$(TARGET_COPY_OUT_SYSTEM)/bin/usb-insert-hook.sh

# Device tree overlays
PRODUCT_PACKAGE_OVERLAYS += \
    device/google/panther/overlay-halium

