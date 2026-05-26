// Copyright (C) 2024 The Android-x86 Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include <android/hardware/thermal/2.0/IThermal.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

#include "ThermalHelper.h"

#include <mutex>
#include <vector>

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::interfacesEqual;
using ::android::hardware::thermal::V1_0::CpuUsage;
using ::android::hardware::thermal::V1_0::Temperature;
using ::android::hardware::thermal::V1_0::ThermalStatus;
using ::android::hardware::thermal::V1_0::ThermalStatusCode;
using ::android::hardware::thermal::V2_0::CoolingDevice;
using ::android::hardware::thermal::V2_0::CoolingType;
using ::android::hardware::thermal::V2_0::IThermalChangedCallback;
using ::android::hardware::thermal::V2_0::Temperature_2_0;
using ::android::hardware::thermal::V2_0::TemperatureThreshold;
using ::android::hardware::thermal::V2_0::TemperatureType;
using ::android::sp;

struct CallbackSetting {
    sp<IThermalChangedCallback> callback;
    bool is_filter_type;
    TemperatureType type;
};

class Thermal : public IThermal {
  public:
    Thermal();
    ~Thermal() override;

    Return<void> getTemperatures(getTemperatures_cb _hidl_cb) override;
    Return<void> getCpuUsages(getCpuUsages_cb _hidl_cb) override;
    Return<void> getCoolingDevices(getCoolingDevices_cb _hidl_cb) override;

    Return<void> getCurrentTemperatures(bool filterType, TemperatureType type,
                                      getCurrentTemperatures_cb _hidl_cb) override;
    Return<void> getTemperatureThresholds(bool filterType, TemperatureType type,
                                          getTemperatureThresholds_cb _hidl_cb) override;
    Return<void> getCurrentCoolingDevices(bool filterType, CoolingType type,
                                          getCurrentCoolingDevices_cb _hidl_cb) override;
    Return<void> registerThermalChangedCallback(const sp<IThermalChangedCallback>& callback,
                                                bool filterType, TemperatureType type,
                                                registerThermalChangedCallback_cb _hidl_cb) override;
    Return<void> unregisterThermalChangedCallback(const sp<IThermalChangedCallback>& callback,
                                                  unregisterThermalChangedCallback_cb _hidl_cb) override;

  private:
    void sendThermalChangedCallback(const Temperature_2_0& temp);

    ThermalHelper helper_;
    std::mutex callback_mutex_;
    std::vector<CallbackSetting> callbacks_;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
