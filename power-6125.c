/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * *    * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_NIDEBUG 0

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>

#define LOG_TAG "QTI PowerHAL"
#include <log/log.h>
#include <hardware/hardware.h>
#include <hardware/power.h>

#include "utils.h"
#include "metadata-defs.h"
#include "hint-data.h"
#include "performance.h"
#include "power-common.h"

#define MIN_VAL(X,Y) ((X>Y)?(Y):(X))

static int video_encode_hint_sent;

const int kMinInteractiveDuration = 100;  /* ms */
const int kMaxInteractiveDuration = 5000; /* ms */
const int kMaxLaunchDuration = 5000;      /* ms */

static int camera_hint_ref_count;
static void process_video_encode_hint(void *metadata);
static void process_video_encode_hfr_hint(void *metadata);
static void process_interaction_hint(void* data);
static int process_activity_launch_hint(void* data);

int power_hint_override(power_hint_t hint, void *data)
{
    switch(hint) {
        case POWER_HINT_LOW_POWER:
        case POWER_HINT_SUSTAINED_PERFORMANCE:
        case POWER_HINT_DISABLE_TOUCH:
        case POWER_HINT_VR_MODE:
        case POWER_HINT_VSYNC:
            break;
        case POWER_HINT_VIDEO_ENCODE:
            process_video_encode_hint(data);
            return HINT_HANDLED;
        /* Using VIDEO_DECODE hint for HR use cases */
        case POWER_HINT_VIDEO_DECODE:
            process_video_encode_hfr_hint(data);
            return HINT_HANDLED;
        case POWER_HINT_INTERACTION:
            process_interaction_hint(data);
            return HINT_HANDLED;
        case POWER_HINT_LAUNCH:
            process_activity_launch_hint(data);
            return HINT_HANDLED;
    }
    return HINT_NONE;
}

int  set_interactive_override(int UNUSED(on))
{
    return HINT_HANDLED; /* to set hints for display on and off. Not in use now */
}

/* Video Encode Hint */
static void process_video_encode_hint(void *metadata)
{
    char governor[80] = {0};
    int resource_values[32] = {0};
    int num_resources = 0;
    struct video_encode_metadata_t video_encode_metadata;

    ALOGI("Got process_video_encode_hint");

    if (get_scaling_governor(governor, sizeof(governor)) == -1) {
        ALOGE("Can't obtain scaling governor.");
        // return HINT_HANDLED;
    }

    /* Initialize encode metadata struct fields. */
    memset(&video_encode_metadata, 0, sizeof(struct video_encode_metadata_t));
    video_encode_metadata.state = -1;
    video_encode_metadata.hint_id = DEFAULT_VIDEO_ENCODE_HINT_ID;

    if (metadata) {
        if (parse_video_encode_metadata((char *)metadata,
            &video_encode_metadata) == -1) {
            ALOGE("Error occurred while parsing metadata.");
            return;
        }
    } else {
        return;
    }

    if (video_encode_metadata.state == 1) {
        if((strncmp(governor, SCHEDUTIL_GOVERNOR,
            strlen(SCHEDUTIL_GOVERNOR)) == 0) &&
            (strlen(governor) == strlen(SCHEDUTIL_GOVERNOR))) {
                /* sample_ms = 20mS
                * hispeed load for both clusters = 95
                * sched_load_boost on all cores = -15
                * silver max freq = 1612 */
                int res[] = {
                             0x41820000, 0x14,
                             0x41440100, 0x5f,
                             0x41440000, 0x5f,
                             0x40C68100, 0xFFFFFFF1,
                             0x40C68110, 0xFFFFFFF1,
                             0x40C68120, 0xFFFFFFF1,
                             0x40C68130, 0xFFFFFFF1,
                             0x40C68000, 0xFFFFFFF1,
                             0x40C68010, 0xFFFFFFF1,
                             0x40C68020, 0xFFFFFFF1,
                             0x40C68030, 0xFFFFFFF1,
                             0x40804100, 0x64C,
                            };
                memcpy(resource_values, res, MIN_VAL(sizeof(resource_values), sizeof(res)));
                num_resources = sizeof(res)/sizeof(res[0]);
                camera_hint_ref_count++;
                if (camera_hint_ref_count == 1) {
                    if (!video_encode_hint_sent) {
                        perform_hint_action(video_encode_metadata.hint_id,
                        resource_values, num_resources);
                        video_encode_hint_sent = 1;
                    }
                }
        }
    } if (video_encode_metadata.state == 0) {
        if (is_interactive_governor(governor) || is_schedutil_governor(governor)) {
            camera_hint_ref_count--;
            if (!camera_hint_ref_count) {
                undo_hint_action(video_encode_metadata.hint_id);
                video_encode_hint_sent = 0;
            }
            return ;
        }
    }
    return;
}

/* Video Encode Hint for HFR use cases */
static void process_video_encode_hfr_hint(void *metadata)
{
    char governor[80] = {0};
    int resource_values[32] = {0};
    int num_resources = 0;
    struct video_encode_metadata_t video_encode_metadata;

    ALOGI("Got process_video_encode_hint for HFR");

    if (get_scaling_governor(governor, sizeof(governor)) == -1) {
        ALOGE("Can't obtain scaling governor.");
        // return HINT_HANDLED;
    }

    /* Initialize encode metadata struct fields. */
    memset(&video_encode_metadata, 0, sizeof(struct video_encode_metadata_t));
    video_encode_metadata.state = -1;
    video_encode_metadata.hint_id = DEFAULT_VIDEO_ENCODE_HINT_ID;

    if (metadata) {
        if (parse_video_encode_metadata((char *)metadata,
            &video_encode_metadata) == -1) {
            ALOGE("Error occurred while parsing metadata.");
            return;
        }
    } else {
        return;
    }

    if (video_encode_metadata.state == 1) {
        if((strncmp(governor, SCHEDUTIL_GOVERNOR,
            strlen(SCHEDUTIL_GOVERNOR)) == 0) &&
            (strlen(governor) == strlen(SCHEDUTIL_GOVERNOR))) {
                /* sample_ms = 20mS
                * hispeed load = 95
                * hispeed freq = 1017 */
                int res[] = {0x41820000, 0x14,
                             0x41440100, 0x5f,
                             0x4143c100, 0x3f9,
                            };
                memcpy(resource_values, res, MIN_VAL(sizeof(resource_values), sizeof(res)));
                num_resources = sizeof(res)/sizeof(res[0]);
                camera_hint_ref_count++;
                if (camera_hint_ref_count == 1) {
                    if (!video_encode_hint_sent) {
                        perform_hint_action(video_encode_metadata.hint_id,
                        resource_values, num_resources);
                        video_encode_hint_sent = 1;
                    }
                }
        }
    } if (video_encode_metadata.state == 0) {
        if (is_interactive_governor(governor) || is_schedutil_governor(governor)) {
            camera_hint_ref_count--;
            if (!camera_hint_ref_count) {
                undo_hint_action(video_encode_metadata.hint_id);
                video_encode_hint_sent = 0;
            }
            return ;
        }
    }
    return;
}

static void process_interaction_hint(void* data) {
    static struct timespec s_previous_boost_timespec;
    static int s_previous_duration = 0;

    struct timespec cur_boost_timespec;
    long long elapsed_time;
    int duration = kMinInteractiveDuration;

    if (data) {
        int input_duration = *((int*)data);
        if (input_duration > duration) {
            duration = (input_duration > kMaxInteractiveDuration) ? kMaxInteractiveDuration
                                                                  : input_duration;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &cur_boost_timespec);

    elapsed_time = calc_timespan_us(s_previous_boost_timespec, cur_boost_timespec);
    // don't hint if it's been less than 250ms since last boost
    // also detect if we're doing anything resembling a fling
    // support additional boosting in case of flings
    if (elapsed_time < 250000 && duration <= 750) {
        return;
    }
    s_previous_boost_timespec = cur_boost_timespec;
    s_previous_duration = duration;

    perf_hint_enable_with_type(VENDOR_HINT_SCROLL_BOOST, duration, SCROLL_VERTICAL);
}

static int process_activity_launch_hint(void* data) {
    static int launch_handle = -1;
    static int launch_mode = 0;

    // release lock early if launch has finished
    if (!data) {
        if (CHECK_HANDLE(launch_handle)) {
            release_request(launch_handle);
            launch_handle = -1;
        }
        launch_mode = 0;
        return HINT_HANDLED;
    }

    if (!launch_mode) {
        launch_handle = perf_hint_enable_with_type(VENDOR_HINT_FIRST_LAUNCH_BOOST,
                                                   kMaxLaunchDuration, LAUNCH_BOOST_V1);
        if (!CHECK_HANDLE(launch_handle)) {
            ALOGE("Failed to perform launch boost");
            return HINT_NONE;
        }
        launch_mode = 1;
    }
    return HINT_HANDLED;
}
