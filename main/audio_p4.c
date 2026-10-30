// Guition JC1060P470C audio (audio.h): I2S0 both ways, as on the AMOLED. Out to the ES8311 (speaker, sound.c; I2C
// 0x18 on touch's bus, NS4150 amplifier enable on GPIO 11, a speaker in the 2-pin MX1.25 socket: none in the case),
// in from the same ES8311's ADC (the microphone behind the case's round holes, DIN 48). MCLK 13, BCLK 12, WS 10,
// DOUT 9. Pins as in a working ESPHome setup for this board (github.com/charrus/Guition-esp32-p4-jc1060p470-...).
#include "audio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "touch.h"

static const char *TAG = "audio";

#define SAMPLE_RATE   16000
#define PIN_MCLK      13
#define PIN_BCLK      12
#define PIN_WS        10
#define PIN_DOUT      9
#define PIN_DIN       48                // ES7210 -> P4 (GPIO 11 is the amplifier enable, GPIO 20 is on the header)

static const audio_codec_data_if_t *data_if;
static SemaphoreHandle_t lock;           // es8311_up(): the sound and presence tasks start together

bool audio_init(void)
{
    if (data_if) return true;
    if (!lock) lock = xSemaphoreCreateMutex();            // app_main, before the tasks that use it
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

// The ES8311 both ways, one device kept open: its ADC is the microphone (I2C 0x18 is the only codec on the bus: no
// ES7210 on this board, whatever its schematic shows; the board's other I2C devices are 0x14/0x5D touch, 0x32, 0x36).
// One device because esp_codec_dev's es8311_open() rewrites the chip's set-up: a second one for the speaker, opened
// at each chime, would stop the microphone. The amplifier is sound.c's (GPIO 11), off between chimes.
static esp_codec_dev_handle_t es8311;

static bool es8311_open_locked(void);

static bool es8311_up(void)
{
    if (es8311) return true;
    if (!audio_init() || !lock) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = es8311 || es8311_open_locked();
    xSemaphoreGive(lock);
    return ok;
}

static bool es8311_open_locked(void)
{
    audio_codec_i2c_cfg_t ccfg = { .port = 0, .addr = 0x30, .bus_handle = touch_i2c_bus() };    // 8-bit address
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&ccfg);
    if (!ctrl_if) return false;
    es8311_codec_cfg_t ecfg = {
        .ctrl_if = ctrl_if, .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH, .pa_pin = -1, .use_mclk = true,
        .no_dac_ref = true,                               // both slots the microphone, not the speaker's signal
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *codec = es8311_codec_new(&ecfg);
    if (!codec) return false;
    esp_codec_dev_cfg_t dcfg = { .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = codec, .data_if = data_if };
    esp_codec_dev_handle_t d = esp_codec_dev_new(&dcfg);
    if (!d) return false;
    esp_codec_dev_sample_info_t fs = { .sample_rate = SAMPLE_RATE, .channel = 2, .bits_per_sample = 16 };
    if (esp_codec_dev_open(d, &fs) != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "ES8311 doesn't answer"); return false; }
    es8311 = d;
    return true;
}

void *audio_speaker_dev(void) { return es8311_up() ? es8311 : NULL; }

bool audio_mic_open(float gain_db)
{
    static bool logged;
    if (!es8311_up()) return false;
    esp_codec_dev_set_in_gain(es8311, gain_db);
    if (!logged) { ESP_LOGI(TAG, "microphone ready (ES8311's input, %.0f dB)", gain_db); logged = true; }
    return true;
}

bool audio_mic_read(int16_t *samples, size_t n)
{
    return es8311 && esp_codec_dev_read(es8311, samples, (int)(n * sizeof(int16_t))) == ESP_CODEC_DEV_OK;
}

const void *audio_data_if(void) { return data_if; }
