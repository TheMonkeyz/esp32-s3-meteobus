// ESP32-P4-Function-EV-Board audio (audio.h): I2S0 out to the ES8311 (speaker, sound.c; I2C 0x18 on touch's bus,
// amplifier enable on GPIO 53). MCLK 13, BCLK 12, WS 10, DOUT 9, DIN 11 (Espressif's BSP). No microphones here for
// presence: audio_mic_open() says "not available" and forge_presence goes by touch.
#include "audio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "audio";

#define SAMPLE_RATE   16000
#define PIN_MCLK      13
#define PIN_BCLK      12
#define PIN_WS        10
#define PIN_DOUT      9
#define PIN_DIN       11

static const audio_codec_data_if_t *data_if;

bool audio_init(void)
{
    if (data_if) return true;
    i2s_chan_handle_t tx = NULL, rx = NULL;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = 4;
    cc.dma_frame_num = 320;
    cc.auto_clear = true;
    if (i2s_new_channel(&cc, &tx, &rx) != ESP_OK) { ESP_LOGE(TAG, "I2S channels failed"); return false; }
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = PIN_MCLK, .bclk = PIN_BCLK, .ws = PIN_WS, .dout = PIN_DOUT, .din = PIN_DIN },
    };
    if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK || i2s_channel_init_std_mode(rx, &sc) != ESP_OK ||
        i2s_channel_enable(tx) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) {
        ESP_LOGE(TAG, "I2S setup failed");
        return false;
    }
    audio_codec_i2s_cfg_t icfg = { .port = I2S_NUM_0, .rx_handle = rx, .tx_handle = tx };
    data_if = audio_codec_new_i2s_data(&icfg);
    return data_if != NULL;
}

bool audio_mic_open(float gain_db) { (void)gain_db; return false; }
bool audio_mic_read(int16_t *samples, size_t n) { (void)samples; (void)n; return false; }
const void *audio_data_if(void) { return data_if; }
