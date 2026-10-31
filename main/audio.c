// The board's audio (audio.h): espforge's forge_audio with this board's pins. The Waveshare AMOLED: I2S0 both ways,
// the ES7210 microphones in (I2C 0x40 on touch's bus) and the ES8311 speaker out (sound.c's own device on the shared
// data interface). The Guition JC1060P470C (ESP32-P4): one ES8311 as speaker and microphone (forge_audio's ES8311_BOTH
// layout: one IN_OUT device kept open under a lock; sound.c writes to it and switches the amplifier, GPIO 11, itself).
// Until espforge v0.6.0 the codec code was here (audio.c) and in audio_p4.c; the pins stay, the rest is forge_audio.c.
#include "audio.h"
#include "forge_audio.h"
#include "esp_codec_dev_defaults.h"              // ES7210_SEL_MIC1/2
#include "touch.h"

static const forge_audio_cfg_t cfg = {
#if CONFIG_IDF_TARGET_ESP32P4
    // Pins as in a working ESPHome setup for this board (github.com/charrus/Guition-esp32-p4-jc1060p470-...). GPIO 11
    // is the amplifier enable (sound.c), GPIO 20 is on the header. The ES8311 (0x18) is the only codec on the bus,
    // whatever the schematic shows (docs/P4-PORT.md).
    .layout = FORGE_AUDIO_ES8311_BOTH,
    .i2s_port = 0,
    .mclk = 13, .bclk = 12, .ws = 10,
    .dout = 9,                                   // P4 -> ES8311 (speaker)
    .din = 48,                                   // ES8311's ADC -> P4 (the microphone behind the case's round holes)
    .sample_rate = 16000,
    .mic_addr = 0x30,                            // the ES8311's 8-bit address (0x18), as esp_codec_dev expects
#else
    .layout = FORGE_AUDIO_ES7210_ES8311,
    .i2s_port = 0,
    .mclk = 42, .bclk = 9, .ws = 45,
    .din = 10,                                   // ES7210 -> ESP32 (the BSP calls it DSIN)
    .dout = 8,                                   // ESP32 -> ES8311 (speaker, sound.c)
    .sample_rate = 16000,
    .mic_addr = 0x80,                            // the ES7210's 8-bit address (0x40), as esp_codec_dev expects
    .mic_select = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
#endif
};

bool audio_init(void)
{
    forge_audio_cfg_t c = cfg;
    c.i2c = touch_i2c_bus();                     // the codecs' control bus is touch's: after touch_init()
    return forge_audio_init(&c, true);           // both directions: the speaker is silent until something plays
}

bool audio_mic_open(float gain_db) { return audio_init() && forge_audio_mic_open(gain_db); }
bool audio_mic_read(int16_t *samples, size_t n) { return forge_audio_mic_read(samples, n); }
const void *audio_data_if(void) { return forge_audio_data_if(); }
void *audio_speaker_dev(void) { return forge_audio_speaker_dev(); }
