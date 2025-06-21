#include <Arduino.h>
#include "menu.h"
#include "debug.h"
#include "board.h"

#define FADE_TIME_MS 16 // don't change: t_fade_mapping is calculated on this value, also 16 is equal to millis() resolution

static const uint8_t bright_mapping[5] = {31, 63, 127, 191, 255};
#if ADC_BITS == 8
static const uint8_t als_mapping[3] = {1, 4, 40}; // 0.01, 0.1, 1 lux
#define VSEN_THRESH_SET 31
#define VSEN_THRESH_CLR 47
#else
static const uint16_t als_mapping[3] = {4, 16, 160}; // 0.01, 0.1, 1 lux
#define VSEN_THRESH_SET 127
#define VSEN_THRESH_CLR 191
#endif

static inline millis_t tout_mapping_get()
{
    static const uint8_t tout_mapping[3] = {0x3B, 0x76, 0xEB};  // 15.104, 30.208, 60.160 seconds
    static const uint8_t t_fade_mapping[5] = {2, 4, 8, 12, 16}; // 512, 1024, 2048, 3072, 4096 ms
    // subtract the fade time from the timeout
    return (((millis_t)(tout_mapping[settings.n_timeout] - t_fade_mapping[settings.n_bright])) << 8);
}

enum
{
    mode_emergency_only,
    mode_night_only,
    mode_dual,
    mode_emergency_night,
    mode_emergency_motion
};

void setup()
{
    DEBUG_INIT();
    DEBUG("Starting...\n");
    board_init();
    settings_init();
}

enum
{
    light_state_idle,
    light_state_emg,
    light_state_fdi,
    light_state_night,
    light_state_fdo
};

void loop()
{
    static uint8_t light_state = light_state_idle;
    static adc_t adc_vsn, adc_als = ADC_VAL_INIT;
    static millis_t millis_prev = 0;
    static uint8_t fade_cnt = 0;

    const millis_t millis_now = millis();
    menu_update(millis_now);

    if (menu_state == menu_state_off)
    {
        uint16_t adc_val = ADC_VAL_GET();
        if (ADC_COMPLETE())
        {
            if (ADC_MUX_GET() == VSN_ADC)
            {
                adc_vsn = adc_val;
                ADC_MUX_SET(ALS_ADC);
            }
            else
            {
                adc_als = adc_val;
                ADC_MUX_SET(VSN_ADC);
            }
            ADC_START();
        }

        bool power_out = (adc_vsn < VSEN_THRESH_SET);
        bool power_in = (adc_vsn > VSEN_THRESH_CLR);
        bool light_out = (adc_als < als_mapping[settings.n_thresh]);

        switch (light_state)
        {
        case light_state_idle:
            if (power_out && (settings.mode == mode_emergency_only || settings.mode == mode_dual /*|| (light_out && (settings.mode == mode_emergency_night))*/))
            {
                light_state = light_state_emg;
                SET_LED(bright_mapping[settings.e_bright]);
            }
            else if (light_out && PIR_DETECT() && (settings.mode == mode_night_only || settings.mode == mode_dual /*|| (power_out && (settings.mode == mode_emergency_motion))*/))
            {
                light_state = light_state_fdi;
                fade_cnt = 0;
                millis_prev = millis_now;
            }
            break;
        case light_state_emg:
            if (power_in)
            {
                SET_LED(0);
                light_state = light_state_idle;
            }
            break;
        case light_state_fdi:
            if (millis_now - millis_prev > FADE_TIME_MS)
            {
                millis_prev = millis_now;
                if (fade_cnt == bright_mapping[settings.n_bright])
                {
                    light_state = light_state_night;
                }
                else
                {
                    SET_LED(fade_cnt++);
                }
            }
            break;
        case light_state_night:
            if (PIR_DETECT())
            {
                millis_prev = millis_now;
            }
            else if ((millis_now - millis_prev > tout_mapping_get()) /*|| (power_in && (settings.mode == mode_emergency_motion))*/)
            {
                light_state = light_state_fdo;
                millis_prev = millis_now;
            }
            break;
        case light_state_fdo:
            if (millis_now - millis_prev > FADE_TIME_MS)
            {
                millis_prev = millis_now;
                if (fade_cnt == 0)
                {
                    light_state = light_state_idle;
                }
                else if (PIR_DETECT())
                {
                    light_state = light_state_fdi;
                }
                else
                {
                    SET_LED(fade_cnt--);
                }
            }
            break;
        default:
            break;
        }
    }
}
