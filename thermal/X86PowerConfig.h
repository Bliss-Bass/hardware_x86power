// Copyright (C) 2024 The Android-x86 Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace android {
namespace hardware {
namespace thermal {
namespace x86power {

struct ThrottleFreqConfig {
    int light = 100;
    int moderate = 85;
    int severe = 70;
    int critical = 50;
    int emergency = 30;
    int shutdown = 20;
};

enum class OfflineCoresLevel {
    NONE = 0,
    SEVERE,
    CRITICAL,
    EMERGENCY,
};

struct X86PowerConfig {
    bool enabled = true;
    int poll_interval_ms = 2000;
    int config_reload_polls = 30;
    float hysteresis_c = 2.0f;

    // "x86_pkg_temp" reads one zone; "max" uses the hottest allowed zone.
    std::string primary_sensor = "x86_pkg_temp";
    std::vector<std::string> ignore_sensors = {
            "SEN3",
            "acpitz",
            "INT3400 Thermal",
            "iwlwifi_1",
    };

    std::array<float, 7> hot_thresholds_c = {
            NAN, 60.0f, 70.0f, 80.0f, 85.0f, 90.0f, 95.0f,
    };

    bool throttle_enabled = true;
    ThrottleFreqConfig throttle_freq_pct{};
    OfflineCoresLevel offline_cores_level = OfflineCoresLevel::CRITICAL;

    void reloadFromProperties();
};

}  // namespace x86power
}  // namespace thermal
}  // namespace hardware
}  // namespace android
