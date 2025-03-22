#include <Arduino.h>
#include "menu.h"
#include "debug.h"
#include "board.h"

#define FADE_TIME_MS 10

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
static const uint8_t tout_mapping[4] = {0x3B, 0x76, 0xB0, 0xEB}; // 15.104, 30.208, 45.056, 60.160 seconds

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
        if (ADC_COMPLETE())
        {
            if (ADC_MUX_GET() == VSN_ADC)
            {
                adc_vsn = ADC_VAL_GET();
                ADC_MUX_SET(ALS_ADC);
            }
            else
            {
                adc_als = ADC_VAL_GET();
                ADC_MUX_SET(VSN_ADC);
            }
            ADC_START();
        }

        switch (light_state)
        {
        case light_state_idle:
            if (adc_vsn < VSEN_THRESH_SET && settings.e_enable)
            {
                light_state = light_state_emg;
                SET_LED(bright_mapping[settings.e_bright]);
            }
            else if (adc_als < als_mapping[settings.n_thresh] && PIR_DETECT() && settings.n_enable)
            {
                light_state = light_state_fdi;
                fade_cnt = 0;
                millis_prev = millis_now;
            }
            break;
        case light_state_emg:
            if (adc_vsn > VSEN_THRESH_CLR)
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
            else if (millis_now - millis_prev > (((millis_t)tout_mapping[settings.n_timeout]) << 8))
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
