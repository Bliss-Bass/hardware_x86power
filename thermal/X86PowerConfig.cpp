// Copyright (C) 2024 The Android-x86 Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0

#define LOG_TAG "X86PowerConfig"

#include "X86PowerConfig.h"

#include <android-base/properties.h>
#include <android-base/strings.h>
#include <utils/Log.h>

#include <cmath>
#include <cstdlib>

namespace android {
namespace hardware {
namespace thermal {
namespace x86power {

namespace {

constexpr const char* kPropPrefix = "persist.x86power.thermal.";

float readFloatProperty(const char* suffix, float default_value) {
    const std::string key = std::string(kPropPrefix) + suffix;
    const std::string value = android::base::GetProperty(key, "");
    if (value.empty()) {
        return default_value;
    }
    char* end = nullptr;
    const float parsed = strtof(value.c_str(), &end);
    if (end == value.c_str() || !std::isfinite(parsed)) {
        ALOGW("Invalid float for %s=%s, using default %.1f", key.c_str(), value.c_str(),
              default_value);
        return default_value;
    }
    return parsed;
}

int readIntProperty(const char* suffix, int default_value, int min_value, int max_value) {
    const std::string key = std::string(kPropPrefix) + suffix;
    const std::string value = android::base::GetProperty(key, "");
    if (value.empty()) {
        return default_value;
    }
    char* end = nullptr;
    const long parsed = strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || parsed < min_value || parsed > max_value) {
        ALOGW("Invalid int for %s=%s, using default %d", key.c_str(), value.c_str(),
              default_value);
        return default_value;
    }
    return static_cast<int>(parsed);
}

bool readBoolProperty(const char* suffix, bool default_value) {
    const std::string key = std::string(kPropPrefix) + suffix;
    return android::base::GetBoolProperty(key, default_value);
}

std::string readStringProperty(const char* suffix, const std::string& default_value) {
    const std::string key = std::string(kPropPrefix) + suffix;
    const std::string value = android::base::GetProperty(key, default_value);
    return value.empty() ? default_value : value;
}

OfflineCoresLevel readOfflineCoresLevel() {
    const std::string value =
            android::base::GetProperty(std::string(kPropPrefix) + "throttle_offline_cores", "critical");
    if (value == "none") {
        return OfflineCoresLevel::NONE;
    }
    if (value == "severe") {
        return OfflineCoresLevel::SEVERE;
    }
    if (value == "emergency") {
        return OfflineCoresLevel::EMERGENCY;
    }
    return OfflineCoresLevel::CRITICAL;
}

std::vector<std::string> parseSensorList(const std::string& value) {
    std::vector<std::string> sensors;
    for (const auto& token : android::base::Split(value, ",")) {
        const std::string trimmed = android::base::Trim(token);
        if (!trimmed.empty()) {
            sensors.push_back(trimmed);
        }
    }
    return sensors;
}

}  // namespace

void X86PowerConfig::reloadFromProperties() {
    enabled = readBoolProperty("enabled", enabled);
    poll_interval_ms = readIntProperty("poll_interval_ms", poll_interval_ms, 250, 60000);
    config_reload_polls = readIntProperty("config_reload_polls", config_reload_polls, 1, 10000);
    hysteresis_c = readFloatProperty("hysteresis_c", hysteresis_c);

    primary_sensor = readStringProperty("primary_sensor", primary_sensor);

    const std::string ignore_value = readStringProperty(
            "ignore_sensors", "SEN3,acpitz,INT3400 Thermal,iwlwifi_1");
    ignore_sensors = parseSensorList(ignore_value);

    hot_thresholds_c[1] = readFloatProperty("threshold_light", hot_thresholds_c[1]);
    hot_thresholds_c[2] = readFloatProperty("threshold_moderate", hot_thresholds_c[2]);
    hot_thresholds_c[3] = readFloatProperty("threshold_severe", hot_thresholds_c[3]);
    hot_thresholds_c[4] = readFloatProperty("threshold_critical", hot_thresholds_c[4]);
    hot_thresholds_c[5] = readFloatProperty("threshold_emergency", hot_thresholds_c[5]);
    hot_thresholds_c[6] = readFloatProperty("threshold_shutdown", hot_thresholds_c[6]);

    throttle_enabled = readBoolProperty("throttle_enabled", throttle_enabled);
    throttle_freq_pct.light =
            readIntProperty("throttle_freq_light", throttle_freq_pct.light, 10, 100);
    throttle_freq_pct.moderate =
            readIntProperty("throttle_freq_moderate", throttle_freq_pct.moderate, 10, 100);
    throttle_freq_pct.severe =
            readIntProperty("throttle_freq_severe", throttle_freq_pct.severe, 10, 100);
    throttle_freq_pct.critical =
            readIntProperty("throttle_freq_critical", throttle_freq_pct.critical, 10, 100);
    throttle_freq_pct.emergency =
            readIntProperty("throttle_freq_emergency", throttle_freq_pct.emergency, 10, 100);
    throttle_freq_pct.shutdown =
            readIntProperty("throttle_freq_shutdown", throttle_freq_pct.shutdown, 10, 100);

    offline_cores_level = readOfflineCoresLevel();

    ALOGI("thermal config: enabled=%d sensor=%s poll=%dms throttle=%d offline=%d",
          enabled, primary_sensor.c_str(), poll_interval_ms, throttle_enabled,
          static_cast<int>(offline_cores_level));
}

}  // namespace x86power
}  // namespace thermal
}  // namespace hardware
}  // namespace android
