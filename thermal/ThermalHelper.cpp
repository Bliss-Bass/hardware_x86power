// Copyright (C) 2024 The Android-x86 Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0

#define LOG_TAG "X86ThermalHelper"

#include "ThermalHelper.h"

#include "Throttler.h"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <dirent.h>
#include <utils/Log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

namespace {

constexpr const char* kThermalRoot = "/sys/class/thermal";
constexpr const char* kCpuSensorName = "TCPU";

}  // namespace

ThermalHelper::ThermalHelper(NotificationCallback cb)
    : notification_cb_(std::move(cb)), throttler_(new Throttler()) {
    config_.reloadFromProperties();
    current_temp_ = buildTemperature(0.0f, ThrottlingSeverity::NONE);
}

ThermalHelper::~ThermalHelper() {
    stop();
    delete throttler_;
}

bool ThermalHelper::start() {
    if (running_.exchange(true)) {
        return true;
    }

    refreshThermalZones();
    throttler_->updateConfig(config_);
    if (!throttler_->start()) {
        running_ = false;
        return false;
    }

    monitor_thread_ = std::thread(&ThermalHelper::monitorLoop, this);
    return true;
}

void ThermalHelper::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    throttler_->stop();
}

bool ThermalHelper::getCurrentTemperatures(bool filter_type, TemperatureType type,
                                           std::vector<Temperature_2_0>* out) {
    if (out == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    if (filter_type && type != TemperatureType::CPU) {
        return false;
    }
    *out = {current_temp_};
    return true;
}

bool ThermalHelper::getTemperatureThresholds(bool filter_type, TemperatureType type,
                                             std::vector<TemperatureThreshold>* out) {
    if (out == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    if (filter_type && type != TemperatureType::CPU) {
        return false;
    }
    *out = {buildThresholds()};
    return true;
}

ThrottlingSeverity ThermalHelper::currentSeverity() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_severity_;
}

void ThermalHelper::refreshThermalZones() {
    zones_.clear();

    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(kThermalRoot), closedir);
    if (!dir) {
        PLOG(ERROR) << "Failed to open " << kThermalRoot;
        return;
    }

    while (dirent* entry = readdir(dir.get())) {
        if (entry->d_type != DT_DIR && entry->d_type != DT_LNK) {
            continue;
        }
        if (!android::base::StartsWith(entry->d_name, "thermal_zone")) {
            continue;
        }

        const std::string zone_path = std::string(kThermalRoot) + "/" + entry->d_name;
        std::string type;
        if (!android::base::ReadFileToString(zone_path + "/type", &type)) {
            continue;
        }
        type = android::base::Trim(type);
        if (type.empty() || isIgnoredSensor(type)) {
            continue;
        }

        zones_.push_back(ThermalZone{type, zone_path});
        ALOGI("Found thermal zone %s at %s", type.c_str(), zone_path.c_str());
    }
}

bool ThermalHelper::readZoneTemp(const ThermalZone& zone, float* out_c) const {
    std::string temp_raw;
    if (!android::base::ReadFileToString(zone.path + "/temp", &temp_raw)) {
        return false;
    }
    temp_raw = android::base::Trim(temp_raw);
    if (temp_raw.empty()) {
        return false;
    }

    char* end = nullptr;
    const long milli_c = strtol(temp_raw.c_str(), &end, 10);
    if (end == temp_raw.c_str()) {
        return false;
    }

    const float temp_c = static_cast<float>(milli_c) / 1000.0f;
    if (!std::isfinite(temp_c) || temp_c < -40.0f || temp_c > 150.0f) {
        ALOGW("Ignoring out-of-range temp %.1fC from %s", temp_c, zone.name.c_str());
        return false;
    }

    *out_c = temp_c;
    return true;
}

float ThermalHelper::readPrimaryTemperatureC() {
    if (zones_.empty()) {
        refreshThermalZones();
    }
    if (zones_.empty()) {
        return NAN;
    }

    if (config_.primary_sensor != "max") {
        for (const auto& zone : zones_) {
            if (zone.name == config_.primary_sensor) {
                float temp_c = NAN;
                if (readZoneTemp(zone, &temp_c)) {
                    return temp_c;
                }
                ALOGW("Failed to read primary sensor %s", zone.name.c_str());
                break;
            }
        }
    }

    float max_temp = -std::numeric_limits<float>::infinity();
    bool found = false;
    for (const auto& zone : zones_) {
        float temp_c = NAN;
        if (!readZoneTemp(zone, &temp_c)) {
            continue;
        }
        if (temp_c > max_temp) {
            max_temp = temp_c;
            found = true;
        }
    }
    return found ? max_temp : NAN;
}

ThrottlingSeverity ThermalHelper::computeSeverity(float temp_c) const {
    if (!std::isfinite(temp_c)) {
        return ThrottlingSeverity::NONE;
    }

    for (int severity = static_cast<int>(ThrottlingSeverity::SHUTDOWN);
         severity >= static_cast<int>(ThrottlingSeverity::LIGHT); --severity) {
        const float threshold = config_.hot_thresholds_c[severity];
        if (std::isfinite(threshold) && temp_c >= threshold) {
            return static_cast<ThrottlingSeverity>(severity);
        }
    }
    return ThrottlingSeverity::NONE;
}

ThrottlingSeverity ThermalHelper::applyHysteresis(ThrottlingSeverity candidate,
                                                  float temp_c) const {
    if (candidate >= current_severity_) {
        return candidate;
    }

    const int current = static_cast<int>(current_severity_);
    const float threshold = config_.hot_thresholds_c[current];
    if (!std::isfinite(threshold)) {
        return candidate;
    }

    if (temp_c >= threshold - config_.hysteresis_c) {
        return current_severity_;
    }
    return candidate;
}

Temperature_2_0 ThermalHelper::buildTemperature(float temp_c,
                                                ThrottlingSeverity severity) const {
    Temperature_2_0 temp{};
    temp.type = TemperatureType::CPU;
    temp.name = kCpuSensorName;
    temp.value = temp_c;
    temp.throttlingStatus = severity;
    return temp;
}

TemperatureThreshold ThermalHelper::buildThresholds() const {
    TemperatureThreshold threshold{};
    threshold.type = TemperatureType::CPU;
    threshold.name = kCpuSensorName;
    threshold.hotThrottlingThresholds = config_.hot_thresholds_c;
    threshold.coldThrottlingThresholds = {NAN, NAN, NAN, NAN, NAN, NAN, NAN};
    return threshold;
}

bool ThermalHelper::isIgnoredSensor(const std::string& name) const {
    for (const auto& ignored : config_.ignore_sensors) {
        if (name == ignored) {
            return true;
        }
    }
    return false;
}

void ThermalHelper::monitorLoop() {
    while (running_) {
        if (++polls_since_reload_ >= config_.config_reload_polls) {
            polls_since_reload_ = 0;
            config_.reloadFromProperties();
            throttler_->updateConfig(config_);
            refreshThermalZones();
        }

        if (!config_.enabled) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.poll_interval_ms));
            continue;
        }

        const float temp_c = readPrimaryTemperatureC();
        ThrottlingSeverity severity = computeSeverity(temp_c);

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            severity = applyHysteresis(severity, temp_c);

            const bool changed = severity != current_severity_ ||
                                 !std::isfinite(current_temp_.value) ||
                                 std::fabs(current_temp_.value - temp_c) >= 0.5f;

            current_severity_ = severity;
            current_temp_ = buildTemperature(temp_c, severity);

            if (changed) {
                ALOGI("CPU temp %.1fC severity %d", temp_c, static_cast<int>(severity));
                if (notification_cb_) {
                    notification_cb_(current_temp_);
                }
            }
        }

        if (config_.throttle_enabled) {
            throttler_->notifySeverity(severity);
        } else {
            throttler_->notifySeverity(ThrottlingSeverity::NONE);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(config_.poll_interval_ms));
    }
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
