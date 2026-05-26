// Copyright (C) 2024 The Android-x86 Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0

#define LOG_TAG "X86ThermalHAL"

#include "Thermal.h"

#include <android-base/logging.h>
#include <hidl/HidlTransportSupport.h>

using android::OK;
using android::sp;
using android::status_t;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::thermal::V2_0::IThermal;
using android::hardware::thermal::V2_0::implementation::Thermal;

int main(int /*argc*/, char** /*argv*/) {
    LOG(INFO) << "x86power Thermal HAL 2.0 starting";

    sp<IThermal> service = new Thermal();
    if (service == nullptr) {
        LOG(ERROR) << "Failed to create Thermal HAL instance";
        return 1;
    }

    configureRpcThreadpool(1, true /* callerWillJoin */);
    status_t status = service->registerAsService();
    if (status != OK) {
        LOG(ERROR) << "Could not register Thermal HAL service: " << status;
        return 1;
    }

    LOG(INFO) << "x86power Thermal HAL ready";
    joinRpcThreadpool();
    return 1;
}
