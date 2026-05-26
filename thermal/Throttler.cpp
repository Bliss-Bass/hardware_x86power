// Copyright (C) 2024 The Android-x86 Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0

#define LOG_TAG "X86Throttler"

#include "Throttler.h"

#include <android-base/file.h>
#include <utils/Log.h>

#include <chrono>
#include <cstdio>
#include <limits>
#include <string>
#include <unistd.h>

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

namespace {

constexpr const char* kCpuRoot = "/sys/devices/system/cpu";
constexpr const char* kOnline = "1";
constexpr const char* kOffline = "0";

bool writeCpuFile(int cpu, const char* suffix, const char* value) {
    char path[256];
    snprintf(path, sizeof(path), "%s/cpu%d/%s", kCpuRoot, cpu, suffix);
    if (!android::base::WriteStringToFile(value, path)) {
        return false;
    }
    return true;
}

bool cpuExists(int cpu) {
    char path[256];
    snprintf(path, sizeof(path), "%s/cpu%d", kCpuRoot, cpu);
    return access(path, F_OK) == 0;
}

int readCpuMaxFreqKhz() {
    std::string value;
    if (!android::base::ReadFileToString(
                std::string(kCpuRoot) + "/cpu0/cpufreq/cpuinfo_max_freq", &value)) {
        return 0;
    }
    return std::stoi(android::base::Trim(value));
}

int readCpuScalingMaxFreqKhz() {
    std::string value;
    if (!android::base::ReadFileToString(
                std::string(kCpuRoot) + "/cpu0/cpufreq/scaling_max_freq", &value)) {
        return 0;
    }
    return std::stoi(android::base::Trim(value));
}

}  // namespace

Throttler::Throttler() = default;

Throttler::~Throttler() {
    stop();
}

bool Throttler::start() {
    if (running_.exchange(true)) {
        return true;
    }
    saved_max_freq_khz_ = readCpuScalingMaxFreqKhz();
    if (saved_max_freq_khz_ <= 0) {
        saved_max_freq_khz_ = readCpuMaxFreqKhz();
    }
    thread_ = std::thread(&Throttler::throttleLoop, this);
    return true;
}

void Throttler::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    restoreAllLocked();
}

void Throttler::updateConfig(const X86PowerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

void Throttler::notifySeverity(ThrottlingSeverity severity) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_severity_ = severity;
    }
    cv_.notify_one();
}

void Throttler::throttleLoop() {
    while (running_) {
        ThrottlingSeverity severity = ThrottlingSeverity::NONE;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(config_.poll_interval_ms),
                         [this]() {
                             return !running_ || pending_severity_ != applied_severity_;
                         });
            if (!running_) {
                break;
            }
            severity = pending_severity_;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!config_.throttle_enabled) {
            restoreAllLocked();
            continue;
        }
        applyThrottlingLocked(severity);
    }
}

void Throttler::applyThrottlingLocked(ThrottlingSeverity severity) {
    if (severity == applied_severity_) {
        return;
    }

    if (severity == ThrottlingSeverity::NONE) {
        restoreAllLocked();
        return;
    }

    const int freq_pct = freqPercentForSeverity(severity);
    if (!setMaxFreqPercent(freq_pct)) {
        ALOGW("Failed to apply freq cap %d%% for severity %d", freq_pct,
              static_cast<int>(severity));
    } else {
        ALOGI("Applied freq cap %d%% for severity %d", freq_pct, static_cast<int>(severity));
    }

    const bool offline = shouldOfflineCores(severity);
    if (offline && !cores_offlined_) {
        setNonBootCoresOnline(false);
        cores_offlined_ = true;
        ALOGI("Offlined non-boot CPUs for severity %d", static_cast<int>(severity));
    } else if (!offline && cores_offlined_) {
        setNonBootCoresOnline(true);
        cores_offlined_ = false;
        ALOGI("Restored non-boot CPUs");
    }

    applied_severity_ = severity;
}

void Throttler::restoreAllLocked() {
    if (saved_max_freq_khz_ > 0) {
        char value[32];
        snprintf(value, sizeof(value), "%d", saved_max_freq_khz_);
        for (int cpu = 0; cpuExists(cpu); ++cpu) {
            writeCpuFile(cpu, "cpufreq/scaling_max_freq", value);
        }
    }
    if (cores_offlined_) {
        setNonBootCoresOnline(true);
        cores_offlined_ = false;
    }
    applied_severity_ = ThrottlingSeverity::NONE;
    pending_severity_ = ThrottlingSeverity::NONE;
}

bool Throttler::setMaxFreqPercent(int percent) {
    if (percent >= 100) {
        if (saved_max_freq_khz_ > 0) {
            char value[32];
            snprintf(value, sizeof(value), "%d", saved_max_freq_khz_);
            bool ok = true;
            for (int cpu = 0; cpuExists(cpu); ++cpu) {
                ok = writeCpuFile(cpu, "cpufreq/scaling_max_freq", value) && ok;
            }
            return ok;
        }
        return true;
    }

    const int max_khz = readCpuMaxFreqKhz();
    if (max_khz <= 0) {
        return false;
    }

    const int target = (max_khz * percent) / 100;
    char value[32];
    snprintf(value, sizeof(value), "%d", target);
    bool ok = true;
    for (int cpu = 0; cpuExists(cpu); ++cpu) {
        ok = writeCpuFile(cpu, "cpufreq/scaling_max_freq", value) && ok;
    }
    return ok;
}

void Throttler::setNonBootCoresOnline(bool online) {
    const char* state = online ? kOnline : kOffline;
    for (int cpu = 1;; ++cpu) {
        char path[256];
        snprintf(path, sizeof(path), "%s/cpu%d/online", kCpuRoot, cpu);
        if (!android::base::WriteStringToFile(state, path)) {
            break;
        }
    }
}

int Throttler::freqPercentForSeverity(ThrottlingSeverity severity) const {
    switch (severity) {
        case ThrottlingSeverity::LIGHT:
            return config_.throttle_freq_pct.light;
        case ThrottlingSeverity::MODERATE:
            return config_.throttle_freq_pct.moderate;
        case ThrottlingSeverity::SEVERE:
            return config_.throttle_freq_pct.severe;
        case ThrottlingSeverity::CRITICAL:
            return config_.throttle_freq_pct.critical;
        case ThrottlingSeverity::EMERGENCY:
            return config_.throttle_freq_pct.emergency;
        case ThrottlingSeverity::SHUTDOWN:
            return config_.throttle_freq_pct.shutdown;
        default:
            return 100;
    }
}

bool Throttler::shouldOfflineCores(ThrottlingSeverity severity) const {
    switch (config_.offline_cores_level) {
        case x86power::OfflineCoresLevel::NONE:
            return false;
        case x86power::OfflineCoresLevel::SEVERE:
            return severity >= ThrottlingSeverity::SEVERE;
        case x86power::OfflineCoresLevel::CRITICAL:
            return severity >= ThrottlingSeverity::CRITICAL;
        case x86power::OfflineCoresLevel::EMERGENCY:
            return severity >= ThrottlingSeverity::EMERGENCY;
    }
    return false;
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
