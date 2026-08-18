/*
 * Filtered LiPo battery estimator for the Toucan2.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_toucan_battery_estimator

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include "toucan_battery_estimator.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define SAMPLE_COUNT 5U
#define SAMPLE_SPACING_MS 10U
#define STARTUP_SETTLE_MS 1200U
#define FILTER_DIVISOR 4
#define MAX_PERCENT_STEP 2U
#define RESTORE_MAX_DELTA_MV 200U
#define PERSIST_VOLTAGE_DELTA_MV 25U
#define PERSIST_PERCENT_DELTA 2U
#define PERSISTED_STATE_VERSION 1U
#define SETTINGS_KEY "toucan_battery/state"

struct battery_curve_point {
    uint16_t millivolts;
    uint8_t percent;
};

/* Integer approximation of ZMK's original Adafruit-derived LiPo curve. */
static const struct battery_curve_point battery_curve[] = {
    {3300, 0},  {3450, 1},  {3500, 2},  {3550, 4},  {3600, 9},  {3650, 17},
    {3700, 28}, {3750, 41}, {3800, 54}, {3850, 66}, {3900, 75}, {3950, 82},
    {4000, 88}, {4050, 92}, {4100, 95}, {4150, 98}, {4200, 100},
};

struct estimator_config {
    const struct device *source;
};

struct estimator_data {
    uint16_t raw_millivolts;
    uint16_t measured_millivolts;
    uint16_t filtered_millivolts;
    uint8_t percent;
    bool initialized;

    bool settings_loaded;
    bool restored_valid;
    uint16_t restored_millivolts;
    uint8_t restored_percent;

    bool persisted_valid;
    uint16_t persisted_millivolts;
    uint8_t persisted_percent;
};

struct persisted_battery_state {
    uint16_t millivolts;
    uint8_t percent;
    uint8_t version;
};

static struct estimator_data estimator_data;
static const struct estimator_config estimator_config = {
    .source = DEVICE_DT_GET(DT_PHANDLE(DT_DRV_INST(0), source)),
};

K_MUTEX_DEFINE(estimator_settings_lock);
K_MUTEX_DEFINE(estimator_sample_lock);

static uint32_t unsigned_delta(uint32_t first, uint32_t second) {
    return first > second ? first - second : second - first;
}

static uint8_t millivolts_to_percent(uint16_t millivolts) {
    if (millivolts <= battery_curve[0].millivolts) {
        return battery_curve[0].percent;
    }

    for (size_t i = 1; i < ARRAY_SIZE(battery_curve); i++) {
        const struct battery_curve_point *lower = &battery_curve[i - 1];
        const struct battery_curve_point *upper = &battery_curve[i];

        if (millivolts <= upper->millivolts) {
            uint32_t voltage_span = upper->millivolts - lower->millivolts;
            uint32_t voltage_offset = millivolts - lower->millivolts;
            uint32_t percent_span = upper->percent - lower->percent;
            uint32_t interpolated =
                (voltage_offset * percent_span + voltage_span / 2U) / voltage_span;

            return lower->percent + interpolated;
        }
    }

    return 100U;
}

static int read_source_millivolts(const struct device *source, uint16_t *millivolts) {
    int err = sensor_sample_fetch_chan(source, SENSOR_CHAN_GAUGE_VOLTAGE);
    if (err < 0) {
        return err;
    }

    struct sensor_value voltage;
    err = sensor_channel_get(source, SENSOR_CHAN_GAUGE_VOLTAGE, &voltage);
    if (err < 0) {
        return err;
    }

    int64_t value = (int64_t)voltage.val1 * 1000 + voltage.val2 / 1000;
    if (value < 2500 || value > 4500) {
        return -ERANGE;
    }

    *millivolts = (uint16_t)value;
    return 0;
}

static void sort_samples(uint16_t *samples, size_t count) {
    for (size_t i = 1; i < count; i++) {
        uint16_t value = samples[i];
        size_t insertion = i;

        while (insertion > 0 && samples[insertion - 1] > value) {
            samples[insertion] = samples[insertion - 1];
            insertion--;
        }

        samples[insertion] = value;
    }
}

static int read_median_millivolts(const struct device *source, uint16_t *millivolts,
                                  uint16_t *raw_millivolts) {
    uint16_t samples[SAMPLE_COUNT];
    size_t valid_samples = 0;
    int last_error = -EIO;

    for (size_t i = 0; i < SAMPLE_COUNT; i++) {
        uint16_t sample;
        int err = read_source_millivolts(source, &sample);

        if (err == 0) {
            samples[valid_samples++] = sample;
        } else {
            last_error = err;
            LOG_DBG("Toucan battery sample %u failed (%d)", (unsigned int)i, err);
        }

        if (i + 1U < SAMPLE_COUNT) {
            k_sleep(K_MSEC(SAMPLE_SPACING_MS));
        }
    }

    if (valid_samples < 3U) {
        return last_error;
    }

    sort_samples(samples, valid_samples);
    uint32_t median;
    if ((valid_samples & 1U) != 0U) {
        median = samples[valid_samples / 2U];
    } else {
        median =
            ((uint32_t)samples[valid_samples / 2U - 1U] + samples[valid_samples / 2U]) / 2U;
    }

    *raw_millivolts = (uint16_t)median;
    int32_t calibrated = (int32_t)median + CONFIG_TOUCAN_BATTERY_VOLTAGE_OFFSET_MV;
    *millivolts = (uint16_t)CLAMP(calibrated, 2500, 4500);
    return 0;
}

static int32_t divide_nearest(int32_t value, int32_t divisor) {
    if (value >= 0) {
        return (value + divisor / 2) / divisor;
    }

    return (value - divisor / 2) / divisor;
}

static uint8_t limit_percent_step(uint8_t previous, uint8_t candidate) {
    if (candidate > previous && candidate - previous > MAX_PERCENT_STEP) {
        return previous + MAX_PERCENT_STEP;
    }

    if (previous > candidate && previous - candidate > MAX_PERCENT_STEP) {
        return previous - MAX_PERCENT_STEP;
    }

    return candidate;
}

static void maybe_persist_state(struct estimator_data *data) {
    bool should_save;

    k_mutex_lock(&estimator_settings_lock, K_FOREVER);
    should_save = data->settings_loaded &&
                  (!data->persisted_valid ||
                   unsigned_delta(data->filtered_millivolts, data->persisted_millivolts) >=
                       PERSIST_VOLTAGE_DELTA_MV ||
                   unsigned_delta(data->percent, data->persisted_percent) >=
                       PERSIST_PERCENT_DELTA);
    k_mutex_unlock(&estimator_settings_lock);

    if (!should_save) {
        return;
    }

    struct persisted_battery_state state = {
        .millivolts = data->filtered_millivolts,
        .percent = data->percent,
        .version = PERSISTED_STATE_VERSION,
    };

    int err = settings_save_one(SETTINGS_KEY, &state, sizeof(state));
    if (err < 0) {
        LOG_WRN("Unable to persist Toucan battery estimate (%d)", err);
        return;
    }

    k_mutex_lock(&estimator_settings_lock, K_FOREVER);
    data->persisted_valid = true;
    data->persisted_millivolts = state.millivolts;
    data->persisted_percent = state.percent;
    k_mutex_unlock(&estimator_settings_lock);
}

static void initialize_estimate(struct estimator_data *data, uint16_t measured_millivolts) {
    bool use_restored = false;
    uint16_t restored_millivolts = 0U;
    uint8_t restored_percent = 0U;

    k_mutex_lock(&estimator_settings_lock, K_FOREVER);
    if (data->restored_valid &&
        unsigned_delta(measured_millivolts, data->restored_millivolts) <=
            RESTORE_MAX_DELTA_MV) {
        use_restored = true;
        restored_millivolts = data->restored_millivolts;
        restored_percent = data->restored_percent;
    }
    k_mutex_unlock(&estimator_settings_lock);

    if (use_restored) {
        data->filtered_millivolts = restored_millivolts;
        data->percent = restored_percent;
        LOG_DBG("Restored Toucan battery estimate: %u mV, %u%%",
                (unsigned int)data->filtered_millivolts, (unsigned int)data->percent);
    } else {
        data->filtered_millivolts = measured_millivolts;
        data->percent = millivolts_to_percent(measured_millivolts);
    }

    data->initialized = true;
}

static int estimator_sample_fetch(const struct device *dev, enum sensor_channel channel) {
    if (channel != SENSOR_CHAN_GAUGE_VOLTAGE &&
        channel != SENSOR_CHAN_GAUGE_STATE_OF_CHARGE && channel != SENSOR_CHAN_ALL) {
        return -ENOTSUP;
    }

    const struct estimator_config *config = dev->config;
    struct estimator_data *data = dev->data;

    k_mutex_lock(&estimator_sample_lock, K_FOREVER);

    if (!data->initialized) {
        k_sleep(K_MSEC(STARTUP_SETTLE_MS));
    }

    uint16_t measured_millivolts;
    uint16_t raw_millivolts;
    int err =
        read_median_millivolts(config->source, &measured_millivolts, &raw_millivolts);
    if (err < 0) {
        k_mutex_unlock(&estimator_sample_lock);
        return err;
    }

    data->raw_millivolts = raw_millivolts;
    data->measured_millivolts = measured_millivolts;

    if (!data->initialized) {
        initialize_estimate(data, measured_millivolts);
    } else {
        int32_t delta = (int32_t)measured_millivolts - data->filtered_millivolts;
        int32_t adjustment = divide_nearest(delta, FILTER_DIVISOR);
        data->filtered_millivolts =
            (uint16_t)CLAMP((int32_t)data->filtered_millivolts + adjustment, 2500, 4500);

        uint8_t candidate = millivolts_to_percent(data->filtered_millivolts);
        data->percent = limit_percent_step(data->percent, candidate);
    }

    LOG_DBG("Toucan battery: median %u mV, filtered %u mV, %u%%",
            (unsigned int)data->measured_millivolts,
            (unsigned int)data->filtered_millivolts, (unsigned int)data->percent);
    maybe_persist_state(data);

    k_mutex_unlock(&estimator_sample_lock);
    return 0;
}

int toucan_battery_estimator_get_raw_millivolts(uint16_t *raw_millivolts) {
    if (raw_millivolts == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&estimator_sample_lock, K_FOREVER);
    uint16_t recorded = estimator_data.raw_millivolts;
    k_mutex_unlock(&estimator_sample_lock);

    if (recorded == 0U) {
        return -EAGAIN;
    }

    *raw_millivolts = recorded;
    return 0;
}

int toucan_battery_estimator_read_raw_millivolts(uint16_t *raw_millivolts) {
    if (raw_millivolts == NULL) {
        return -EINVAL;
    }

    uint16_t calibrated_millivolts;

    k_mutex_lock(&estimator_sample_lock, K_FOREVER);
    int err = read_median_millivolts(estimator_config.source, &calibrated_millivolts,
                                     raw_millivolts);
    k_mutex_unlock(&estimator_sample_lock);

    return err;
}

static int estimator_channel_get(const struct device *dev, enum sensor_channel channel,
                                 struct sensor_value *value) {
    const struct estimator_data *data = dev->data;

    switch (channel) {
    case SENSOR_CHAN_GAUGE_VOLTAGE:
        value->val1 = data->filtered_millivolts / 1000U;
        value->val2 = (data->filtered_millivolts % 1000U) * 1000U;
        return 0;
    case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
        value->val1 = data->percent;
        value->val2 = 0;
        return 0;
    default:
        return -ENOTSUP;
    }
}

static int estimator_init(const struct device *dev) {
    const struct estimator_config *config = dev->config;

    if (!device_is_ready(config->source)) {
        LOG_ERR("Toucan battery source %s is not ready", config->source->name);
        return -ENODEV;
    }

    return 0;
}

static int estimator_settings_set(const char *name, size_t length, settings_read_cb read_cb,
                                  void *cb_arg) {
    const char *next;

    if (!settings_name_steq(name, "state", &next) || next != NULL ||
        length != sizeof(struct persisted_battery_state)) {
        return -ENOENT;
    }

    struct persisted_battery_state state;
    int err = read_cb(cb_arg, &state, sizeof(state));
    if (err < 0) {
        return err;
    }

    if (err != (int)sizeof(state) || state.version != PERSISTED_STATE_VERSION ||
        state.millivolts < 2500U || state.millivolts > 4500U || state.percent > 100U) {
        return -EINVAL;
    }

    k_mutex_lock(&estimator_settings_lock, K_FOREVER);
    estimator_data.restored_valid = true;
    estimator_data.restored_millivolts = state.millivolts;
    estimator_data.restored_percent = state.percent;
    estimator_data.persisted_valid = true;
    estimator_data.persisted_millivolts = state.millivolts;
    estimator_data.persisted_percent = state.percent;
    k_mutex_unlock(&estimator_settings_lock);

    return 0;
}

static int estimator_settings_commit(void) {
    k_mutex_lock(&estimator_settings_lock, K_FOREVER);
    estimator_data.settings_loaded = true;
    k_mutex_unlock(&estimator_settings_lock);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(toucan_battery, "toucan_battery", NULL,
                               estimator_settings_set, estimator_settings_commit, NULL);

static const struct sensor_driver_api estimator_api = {
    .sample_fetch = estimator_sample_fetch,
    .channel_get = estimator_channel_get,
};

DEVICE_DT_INST_DEFINE(0, estimator_init, NULL, &estimator_data, &estimator_config, POST_KERNEL,
                      CONFIG_TOUCAN_BATTERY_ESTIMATOR_INIT_PRIORITY, &estimator_api);
