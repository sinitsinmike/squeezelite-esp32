/*
 *  Squeezelite for esp32
 *
 *  (c) Philippe G. 2020, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */


#include "esp_equalizer.h"
#include "math.h"
#include "platform_config.h"
#include "squeezelite.h"
#include "equalizer.h"

#define EQ_BANDS 10

static log_level loglevel = lINFO;
double loudness_factor = 0;
double adjusted_gain = 0;

#define NUM_BANDS 10
#define POLYNOME_COUNT 6
static const double loudness_envelope_coefficients[NUM_BANDS][POLYNOME_COUNT] =
    {{5.5169301499257067e+001, 6.3671410796029004e-001,
      -4.2663226432095233e-002, 8.1063072336581246e-004,
      -7.3621858933917722e-006, 2.5349489594339575e-008},
     {3.7716143859944118e+001, 1.2355293276538579e+000,
      -6.6435374582217863e-002, 1.2976763440259382e-003,
      -1.1978732496353172e-005, 4.1664114634622593e-008},
     {2.5103632377146837e+001, 1.3259150615414637e+000,
      -6.6332442135695099e-002, 1.2845279812261677e-003,
      -1.1799885217545631e-005, 4.0925911584040685e-008},
     {1.3159168212144563e+001, 8.8149357628440639e-001,
      -4.0384121097225931e-002, 7.3843501027501322e-004,
      -6.5508794453097008e-006, 2.2221997141120518e-008},
     {5.1337853800151700e+000, 4.0817077967582394e-001,
      -1.4107826528626457e-002, 1.5251066311713760e-004,
      -3.6689819583740298e-007, -2.0390798774727989e-009},
     {3.1432364156464315e-001, 9.1260548140023004e-002,
      -3.5012124633183438e-004, -8.6023911664606992e-005,
      1.6785606828245921e-006, -8.8269731094371646e-009},
     {-4.0965062397075833e+000, 1.3667010948271402e-001,
      2.4775896786988390e-004, -9.6620399661858641e-005,
      1.7733690952379155e-006, -9.1583104942496635e-009},
     {-9.0275786029994176e+000, 2.6226938845184250e-001,
      -6.5777547972402156e-003, 1.0045957188977551e-004,
      -7.8851000325128971e-007, 2.4639885209682384e-009},
     {-4.4275018199195815e+000, 4.5399572638241725e-001,
      -2.4034902766833462e-002, 5.9828953622534668e-004,
      -6.2893971217140864e-006, 2.3133296592719627e-008},
     {1.4243299202697818e+001, 3.6984458807056630e-001,
      -3.0413994109395680e-002, 7.6700105080386904e-004,
      -8.2777185209388079e-006, 3.1352890650784970e-008}};

static struct {
  void *handle;
  float gain[EQ_BANDS];
  bool update;
} equalizer = {.handle = NULL, .gain = {0}, .update = true};

void equalizer_get_loudness_factor() {
  char *config = config_alloc_get_default(NVS_TYPE_STR, "loudness", "0", 0);
  if (!config) {
    LOG_WARN("Equalizer Config not found");
  } else {
    int loudness_level = atoi(config);
    loudness_factor =
        (loudness_level == 0) ? 0 : pow((loudness_level / 100.0), 2);
    free(config);
  }
}
/****************************************************************************************
 * get the equalizer config
 */
s8_t *equalizer_get_config(void) {
  s8_t *pGains = malloc(sizeof(s8_t) * EQ_BANDS);
  memset(pGains, 0x00, sizeof(s8_t) * EQ_BANDS);
  uint8_t num_entries = 0;
  char *config = config_alloc_get(NVS_TYPE_STR, "equalizer");
  if (!config) {
    LOG_WARN("Equalizer Config not found");
  } else {
    char *p = strtok(config, ", !");
    for (int i = 0; p && i < EQ_BANDS; i++) {
      pGains[i] = atoi(p);
      num_entries++;
      p = strtok(NULL, ", :");
    }
    if (num_entries < EQ_BANDS) {
      LOG_ERROR("Invalid equalizer settings. Resetting it");
      memset(pGains, 0x00, sizeof(s8_t) * EQ_BANDS);
    }
    free(config);
  }
  return pGains;
}
/****************************************************************************************
 * update equalizer gain
 */
void equalizer_update(s8_t *gain) {
  char config[EQ_BANDS * 4 + 1] = {};
  int n = 0;
  for (int i = 0; i < EQ_BANDS; i++) {
    equalizer.gain[i] = gain[i];
    n += sprintf(config + n, "%d,", gain[i]);
  }
  config[n - 1] = '\0';
  config_set_value(NVS_TYPE_STR, "equalizer", config);
  equalizer_apply_loudness();
}
/****************************************************************************************
 * initialize equalizer
 */
void equalizer_init(void) {
  s8_t *pGains = equalizer_get_config();
  equalizer_update(pGains);
  LOG_INFO("initializing equalizer, loudness %s",
           loudness_factor > 0 ? "ENABLED" : "DISABLED");
  free(pGains);
}

/****************************************************************************************
 * open equalizer
 */
void equalizer_open(u32_t sample_rate) {
  // in any case, need to clear update flag
  equalizer.update = false;

  if (sample_rate != 11025 && sample_rate != 22050 && sample_rate != 44100 &&
      sample_rate != 48000) {
    LOG_WARN(
        "equalizer only supports 11025, 22050, 44100 and 48000 sample rates, "
        "not %u",
        sample_rate);
    return;
  }

  equalizer.handle = esp_equalizer_init(2, sample_rate, EQ_BANDS, 0);

  if (equalizer.handle) {
    bool active = false;

    for (int i = 0; i < EQ_BANDS; i++) {
      esp_equalizer_set_band_value(equalizer.handle, equalizer.gain[i], i, 0);
      esp_equalizer_set_band_value(equalizer.handle, equalizer.gain[i], i, 1);
      active |= equalizer.gain[i] != 0;
    }

    // do not activate equalizer if all gain are 0
    if (!active)
      equalizer_close();

    LOG_INFO("equalizer initialized %u", active);
  } else {
    LOG_WARN("can't init equalizer");
  }
}

/****************************************************************************************
 * close equalizer
 */
void equalizer_close(void) {
  if (equalizer.handle) {
    esp_equalizer_uninit(equalizer.handle);
    equalizer.handle = NULL;
  }
}
/****************************************************************************************
 * Prints the equalizer settings to the console
 */
void equalizer_print_bands(const char *message, float *values, uint8_t count) {
  assert(count > 0);
  char *bands = malloc(strlen(message) + count * 8 + 1);
  int n = 0;
  assert(values);
  n += sprintf(bands + n, "%s", message);
  for (int i = 0; i < count; i++) {
    n += sprintf(bands + n, "%0.2f,", values[i]);
  }
  bands[n - 1] = '\0';
  LOG_DEBUG("%s", bands);
  free(bands);
}
/****************************************************************************************
 * Calculates loudness values for each band at a given volume
 */
float *calculate_loudness_curve(double volume_level) {
  LOG_DEBUG("Calculating loudness curve for volume level %.3f", volume_level);
  float *loudness_curve = malloc(EQ_BANDS * sizeof(float));
  memset(loudness_curve, 0x00, EQ_BANDS * sizeof(float));
  equalizer_get_loudness_factor();
  for (int i = 0; i < EQ_BANDS && loudness_factor > 0; i++) {
    for (int j = 0; j < POLYNOME_COUNT; j++) {
      loudness_curve[i] +=
          loudness_envelope_coefficients[i][j] * pow(volume_level, j);
    }
    loudness_curve[i] *= loudness_factor;
  }
  equalizer_print_bands("calculated Loudness: ", loudness_curve, EQ_BANDS);
  return loudness_curve;
}

/****************************************************************************************
 * Combine Loudness and user EQ settings and apply them
 */
void equalizer_apply_loudness() {
  s8_t *pGains = equalizer_get_config();
  float *loudness_curve = calculate_loudness_curve(adjusted_gain);
  for (int i = 0; i < EQ_BANDS; i++) {
    equalizer.gain[i] = (float)(loudness_curve[i] + (float)pGains[i]);
  }
  equalizer_print_bands("Combined Loudness: ", equalizer.gain, EQ_BANDS);
  free(loudness_curve);
  free(pGains);
  equalizer.update = true;
}

/****************************************************************************************
 * process equalizer
 */
void equalizer_process(u8_t *buf, u32_t bytes, u32_t sample_rate) {
  // don't want to process with output locked, so take the small risk to miss
  // one parametric update
  if (equalizer.update) {
    equalizer_close();
    equalizer_open(sample_rate);
  }

  if (equalizer.handle)
    esp_equalizer_process(equalizer.handle, buf, bytes, sample_rate, 2);
}

/****************************************************************************************
 * Updates the loudness EQ curve based on a new volume level
 */
void equalizer_set_loudness(unsigned left, unsigned right) {
  LOG_DEBUG("Setting loudness for volume %d/%d", left, right);
  // Calculate the average gain
  unsigned average_gain = (left + right) / 2;
  // Convert the average gain to a logarithmic format (range -60 to 0)
  double log_gain = average_gain > 0
                        ? log2((double)average_gain / (1 << 16)) * 6.0206
                        : -60; // Convert to dB
  adjusted_gain = (log_gain + 60.0) / 60.0 * 100.0;
  equalizer_apply_loudness();
}
