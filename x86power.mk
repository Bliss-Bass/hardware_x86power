# Copyright (C) 2024 The Android-x86 Open Source Project
#
# Vendor integration for the x86power HAL stack (Power HAL + Thermal HAL).
#
# Add to your device makefile:
#   $(call inherit-product, hardware/x86power/x86power.mk)
#
# Override the tree path if this repo is not checked out as hardware/x86power:
#   X86POWER_DIR := vendor/foobar/hardware/x86power

ifneq ($(X86POWER_MAKEFILE),true)
X86POWER_MAKEFILE := true

X86POWER_DIR ?= hardware/x86power

# ---------------------------------------------------------------------------
# Packages (built from Android.bp in this directory)
# ---------------------------------------------------------------------------

# Legacy hw module Power HAL (power.c) -> vendor/lib*/hw/power.x86.so
PRODUCT_PACKAGES += \
    power.x86

# Thermal HAL 2.0 HIDL service (monitor + throttler threads)
PRODUCT_PACKAGES += \
    android.hardware.thermal@2.0-service.x86power

# ---------------------------------------------------------------------------
# Init scripts
# ---------------------------------------------------------------------------

PRODUCT_INIT_RC += \
    $(X86POWER_DIR)/thermal/android.hardware.thermal@2.0-service.x86power.rc

# ---------------------------------------------------------------------------
# VINTF device manifest
# ---------------------------------------------------------------------------

DEVICE_MANIFEST_FILE += \
    $(X86POWER_DIR)/thermal/manifest.xml

# ---------------------------------------------------------------------------
# HAL selection / defaults
# ---------------------------------------------------------------------------

# Load vendor/lib*/hw/power.x86.so for android.hardware.power
PRODUCT_PROPERTY_OVERRIDES += \
    ro.hardware.power=x86

# Sensible defaults for passive-cooled x86_64 targets (override per device).
PRODUCT_PRODUCT_PROPERTIES += \
    persist.x86power.thermal.enabled=true \
    persist.x86power.thermal.primary_sensor=x86_pkg_temp \
    persist.x86power.thermal.throttle_enabled=true \
    persist.x86power.thermal.poll_interval_ms=2000

# Matches the existing power.c screen-off behaviour unless changed at runtime.
PRODUCT_PROPERTY_OVERRIDES += \
    power.nonboot-cpu-off=1

# ---------------------------------------------------------------------------
# Superseded AOSP mock / default HAL packages
# ---------------------------------------------------------------------------

PRODUCT_PACKAGES := $(filter-out \
    android.hardware.thermal@2.0-service-mock \
    android.hardware.thermal@2.0-service \
    android.hardware.thermal@2.0-service.mock \
    android.hardware.thermal@1.0-service-mock \
    android.hardware.thermal@1.0-service \
    android.hardware.thermal@1.0-service.mock \
    ,$(PRODUCT_PACKAGES))

endif
