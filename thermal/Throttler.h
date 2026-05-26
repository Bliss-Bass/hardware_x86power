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
#include <mutex>
#include <thread>

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

using ::android::hardware::thermal::V2_0::ThrottlingSeverity;

class Throttler {
  public:
    Throttler();
    ~Throttler();

    bool start();
    void stop();

    void updateConfig(const X86PowerConfig& config);
    void notifySeverity(ThrottlingSeverity severity);

  private:
    void throttleLoop();
    void applyThrottlingLocked(ThrottlingSeverity severity);
    void restoreAllLocked();
    bool setMaxFreqPercent(int percent);
    void setNonBootCoresOnline(bool online);
    int freqPercentForSeverity(ThrottlingSeverity severity) const;
    bool shouldOfflineCores(ThrottlingSeverity severity) const;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    X86PowerConfig config_;
    ThrottlingSeverity pending_severity_ = ThrottlingSeverity::NONE;
    ThrottlingSeverity applied_severity_ = ThrottlingSeverity::NONE;
    bool cores_offlined_ = false;
    int saved_max_freq_khz_ = 0;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
