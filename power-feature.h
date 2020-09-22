/*
 * Copyright (C) 2015 The CyanogenMod Project
 * Copyright (C) 2019 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _QCOM_POWER_FEATURE_H
#define _QCOM_POWER_FEATURE_H

#include <hardware/power.h>

#ifdef __cplusplus
extern "C" {
#endif

void set_device_specific_mode(feature_t feature, bool state);
bool is_device_specific_mode_supported(feature_t feature);

#ifdef __cplusplus
}
#endif

#endif
