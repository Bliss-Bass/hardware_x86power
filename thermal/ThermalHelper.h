// Copyright (C) 2024 The Android-x86 Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "X86PowerConfig.h"

#include <android/hardware/thermal/2.0/types.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

using ::android::hardware::thermal::V2_0::Temperature_2_0;
using ::android::hardware::thermal::V2_0::TemperatureThreshold;
using ::android::hardware::thermal::V2_0::TemperatureType;
using ::android::hardware::thermal::V2_0::ThrottlingSeverity;

class Throttler;

class ThermalHelper {
  public:
    using NotificationCallback = std::function<void(const Temperature_2_0&)>;

    explicit ThermalHelper(NotificationCallback cb);
    ~ThermalHelper();

    bool start();
    void stop();

    bool getCurrentTemperatures(bool filter_type, TemperatureType type,
                                std::vector<Temperature_2_0>* out);
    bool getTemperatureThresholds(bool filter_type, TemperatureType type,
                                  std::vector<TemperatureThreshold>* out);
    ThrottlingSeverity currentSeverity() const;

  private:
    struct ThermalZone {
        std::string name;
        std::string path;
    };

    void monitorLoop();
    void refreshThermalZones();
    bool readZoneTemp(const ThermalZone& zone, float* out_c) const;
    float readPrimaryTemperatureC();
    ThrottlingSeverity computeSeverity(float temp_c) const;
    ThrottlingSeverity applyHysteresis(ThrottlingSeverity candidate, float temp_c) const;
    Temperature_2_0 buildTemperature(float temp_c, ThrottlingSeverity severity) const;
    TemperatureThreshold buildThresholds() const;
    bool isIgnoredSensor(const std::string& name) const;

    NotificationCallback notification_cb_;
    Throttler* throttler_;

    mutable std::mutex state_mutex_;
    X86PowerConfig config_;
    std::vector<ThermalZone> zones_;
    Temperature_2_0 current_temp_;
    ThrottlingSeverity current_severity_ = ThrottlingSeverity::NONE;

    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    int polls_since_reload_ = 0;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
