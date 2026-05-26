// Copyright (C) 2024 The Android-x86 Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0

#define LOG_TAG "X86ThermalHAL"

#include "Thermal.h"

#include <algorithm>
#include <cmath>

#include <utils/Log.h>

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

namespace {

ThermalStatus successStatus() {
    ThermalStatus status{};
    status.code = ThermalStatusCode::SUCCESS;
    return status;
}

ThermalStatus failureStatus(const char* message) {
    ThermalStatus status{};
    status.code = ThermalStatusCode::FAILURE;
    status.debugMessage = message;
    return status;
}

}  // namespace

Thermal::Thermal()
    : helper_(std::bind(&Thermal::sendThermalChangedCallback, this, std::placeholders::_1)) {
    helper_.start();
}

Thermal::~Thermal() {
    helper_.stop();
}

Return<void> Thermal::getTemperatures(getTemperatures_cb _hidl_cb) {
    std::vector<Temperature_2_0> temps;
    helper_.getCurrentTemperatures(false, TemperatureType::CPU, &temps);

    std::vector<Temperature> legacy;
    for (const auto& temp : temps) {
        Temperature legacy_temp{};
        legacy_temp.type = static_cast<::android::hardware::thermal::V1_0::TemperatureType>(
                static_cast<int32_t>(temp.type));
        legacy_temp.name = temp.name;
        legacy_temp.currentValue = temp.value;
        legacy_temp.throttlingThreshold = NAN;
        legacy_temp.shutdownThreshold = NAN;
        legacy_temp.vrThrottlingThreshold = NAN;
        legacy.push_back(legacy_temp);
    }

    _hidl_cb(successStatus(), legacy);
    return Void();
}

Return<void> Thermal::getCpuUsages(getCpuUsages_cb _hidl_cb) {
    CpuUsage usage{};
    usage.name = "cpu0";
    usage.active = 0;
    usage.total = 0;
    usage.isOnline = true;
    _hidl_cb(successStatus(), {usage});
    return Void();
}

Return<void> Thermal::getCoolingDevices(getCoolingDevices_cb _hidl_cb) {
    _hidl_cb(successStatus(), {});
    return Void();
}

Return<void> Thermal::getCurrentTemperatures(bool filterType, TemperatureType type,
                                             getCurrentTemperatures_cb _hidl_cb) {
    std::vector<Temperature_2_0> temps;
    if (!helper_.getCurrentTemperatures(filterType, type, &temps)) {
        _hidl_cb(failureStatus("Failed to read data"), {});
        return Void();
    }
    _hidl_cb(successStatus(), temps);
    return Void();
}

Return<void> Thermal::getTemperatureThresholds(bool filterType, TemperatureType type,
                                               getTemperatureThresholds_cb _hidl_cb) {
    std::vector<TemperatureThreshold> thresholds;
    if (!helper_.getTemperatureThresholds(filterType, type, &thresholds)) {
        _hidl_cb(failureStatus("Failed to read data"), {});
        return Void();
    }
    _hidl_cb(successStatus(), thresholds);
    return Void();
}

Return<void> Thermal::getCurrentCoolingDevices(bool filterType, CoolingType type,
                                               getCurrentCoolingDevices_cb _hidl_cb) {
    if (filterType) {
        _hidl_cb(failureStatus("No matching cooling device"), {});
        return Void();
    }
    _hidl_cb(successStatus(), {});
    return Void();
}

Return<void> Thermal::registerThermalChangedCallback(
        const sp<IThermalChangedCallback>& callback, bool filterType, TemperatureType type,
        registerThermalChangedCallback_cb _hidl_cb) {
    if (callback == nullptr) {
        _hidl_cb(failureStatus("Invalid nullptr callback"));
        return Void();
    }

    std::lock_guard<std::mutex> lock(callback_mutex_);
    for (const auto& existing : callbacks_) {
        if (interfacesEqual(existing.callback, callback)) {
            _hidl_cb(failureStatus("Same callback interface registered already"));
            return Void();
        }
    }

    callbacks_.push_back({callback, filterType, type});
    ALOGI("Registered thermal callback filter=%d type=%d", filterType, static_cast<int>(type));

    std::vector<Temperature_2_0> temps;
    if (helper_.getCurrentTemperatures(filterType, type, &temps)) {
        for (const auto& temp : temps) {
            callback->notifyThrottling(temp);
        }
    }

    _hidl_cb(successStatus());
    return Void();
}

Return<void> Thermal::unregisterThermalChangedCallback(
        const sp<IThermalChangedCallback>& callback,
        unregisterThermalChangedCallback_cb _hidl_cb) {
    if (callback == nullptr) {
        _hidl_cb(failureStatus("Invalid nullptr callback"));
        return Void();
    }

    bool removed = false;
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callbacks_.erase(
            std::remove_if(callbacks_.begin(), callbacks_.end(),
                           [&](const CallbackSetting& setting) {
                               if (interfacesEqual(setting.callback, callback)) {
                                   removed = true;
                                   return true;
                               }
                               return false;
                           }),
            callbacks_.end());

    if (!removed) {
        _hidl_cb(failureStatus("The callback was not registered before"));
        return Void();
    }

    _hidl_cb(successStatus());
    return Void();
}

void Thermal::sendThermalChangedCallback(const Temperature_2_0& temp) {
    std::vector<CallbackSetting> callbacks_copy;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callbacks_copy = callbacks_;
    }

    for (const auto& setting : callbacks_copy) {
        if (setting.is_filter_type && setting.type != temp.type) {
            continue;
        }
        if (!setting.callback->notifyThrottling(temp).isOk()) {
            ALOGW("Failed to notify thermal callback");
        }
    }
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
