#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <Preferences.h>
#include "passwd.h"
#include <Wire.h>
#include <Adafruit_ADS7830.h>
#include <Adafruit_XCA9554.h>
#include <Adafruit_PWMServoDriver.h>
#include <jled.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SolarCalculator.h>
#include <time.h>
#include <Timezone.h>
#include <PicoSyslog.h>

Preferences prefs;

PicoSyslog::SimpleLogger Syslog;

wl_status_t wifi_status_prev;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "it.pool.ntp.org"); // unneeded time interval, update managed directly
#define TIME_UPDATE_HOUR 3						 // a what time the time should be updated

TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120}; // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};	// Central European Standard Time
Timezone CE(CEST, CET);

#define FW_VERSION 0U
#define FW_BUILD PIO_COMPILE_TIME
#define CPU_LOAD_REPORT_MS 1000
#define CPU_LOAD_OVERHEAD 338
// #define CPU_CYCLES_MULT 8

#define CPU_CYC_EXP ((F_CPU / 1000) * CPU_LOAD_REPORT_MS)
// #define CAL_CPU_CYCLES

#define WIFI_CONN_TIMEOUT 60 // seconds
#define HOSTNAME "corridorLightController"

#define PCA9685_BASE_ADDR 0x40
#define PCA9554_BASE_ADDR 0x38
#define ADS7830_BASE_ADDR 0x48

#define PWM_FREQ_PSC 3

#define CLC_MAIN_CYCLE_UPD_MS 10				   // update interval in milliseconds, 10ms -> 100Hz
#define CLC_SLOW_CYCLE_UPD_MS 60000				   // update interval in milliseconds, 1 min
#define CLC_SECT_NUM 3							   // number of distribution boards
#define CLC_CHAN_NUM 7							   // number of actually used channels per distribution board
#define CLC_TILE_NUM (CLC_SECT_NUM * CLC_CHAN_NUM) // total number of tiles
#define CLC_BTN_CHAN 7							   // channel of the I/O expander used as the button input
// single press = on with auto brightness and timeout, long press = full on, second press = off (auto)
#define CLC_BTN_SHORT_PRESS_TICKS 2	 // number of CLC_UPD_MS ticks to consider a press as a short one (debouncing)
#define CLC_BTN_LONG_PRESS_TICKS 100 // number of CLC_UPD_MS ticks to consider a press as a long one
// for analog control:
// pwm is scaled down and filtered, 100% gives 2.5V while the minimum is 0.3V corresponding to 12%, in 12 bits it's 491
#define CLC_PWM_MIN 491
#define CLC_PWM_MAX 2047		 // TBD
#define CLC_ADC_SAMPLE_COUNT 257 // 257*255=2^16-1

#define CLC_MOTION_TICKS 100 * 10 // motion timeout ticks in 10ms increments, 1000 ticks -> 10 s

#define CLC_DAWNDUSK_ALT CIVIL_DAWNDUSK_STD_ALTITUDE // -6°
#define CLC_DAYLIGHT_ALT 10							 // +10°

enum
{
	button_press_none,
	button_press_short,
	button_press_long
};

enum
{
	sect_mode_auto,
	sect_mode_manual,
	sect_mode_fullon
};

enum
{
	light_state_off,
	light_state_fade_in,
	light_state_on,
	light_state_fade_out
};

Adafruit_PWMServoDriver pwm[CLC_SECT_NUM] = {
	Adafruit_PWMServoDriver(PCA9685_BASE_ADDR + 0),
	Adafruit_PWMServoDriver(PCA9685_BASE_ADDR + 1),
	Adafruit_PWMServoDriver(PCA9685_BASE_ADDR + 2)};

Adafruit_XCA9554 ioe[CLC_SECT_NUM] = {
	Adafruit_XCA9554()};

Adafruit_ADS7830 adc[CLC_SECT_NUM] = {
	Adafruit_ADS7830()};

bool pwm_avail[CLC_SECT_NUM], ioe_avail[CLC_SECT_NUM], adc_avail[CLC_SECT_NUM] = {false};

JLed status_led = JLed(LED_BUILTIN).Breathe(500, 1000, 500).DelayAfter(1000).Forever();

// direct neighbors mask for each channel

/*
| 20 |
| 19 |
| 18 |
| 17 |
| 16 |
| 15 |
| 14 |
| 13 |
| 12 |
| 11 |
| 10 |
| 09 |
| 08 |
| 07 |
| 06 |
| 05 |  02  |
| 04 |  01  |
| 03 |  00  |
*/

const uint32_t clc_direct_neighbors[CLC_TILE_NUM] = {
	_BV(1) | _BV(3),		  // 00
	_BV(0) | _BV(2) | _BV(4), // 01
	_BV(1) | _BV(5),		  // 02
	_BV(0) | _BV(4),		  // 03
	_BV(1) | _BV(3) | _BV(5), // 04
	_BV(2) | _BV(4) | _BV(6), // 05
	_BV(5) | _BV(7),		  // 06
	_BV(6) | _BV(8),		  // 07
	_BV(7) | _BV(9),		  // 08
	_BV(8) | _BV(10),		  // 09
	_BV(9) | _BV(11),		  // 10
	_BV(10) | _BV(12),		  // 11
	_BV(11) | _BV(13),		  // 12
	_BV(12) | _BV(14),		  // 13
	_BV(13) | _BV(15),		  // 14
	_BV(14) | _BV(16),		  // 15
	_BV(15) | _BV(17),		  // 16
	_BV(16) | _BV(18),		  // 17
	_BV(17) | _BV(19),		  // 18
	_BV(18) | _BV(20),		  // 19
	_BV(19)					  // 20
};

const uint32_t clc_indirect_neighbors[CLC_TILE_NUM] = {
	_BV(2) | _BV(4),		  // 00
	_BV(3) | _BV(5),		  // 01
	_BV(0) | _BV(4) | _BV(6), // 02
	_BV(1) | _BV(5),		  // 03
	_BV(0) | _BV(2) | _BV(6), // 04
	_BV(1) | _BV(3) | _BV(7), // 05
	_BV(2) | _BV(4) | _BV(8), // 06
	_BV(5) | _BV(9),		  // 07
	_BV(6) | _BV(10),		  // 08
	_BV(7) | _BV(11),		  // 09
	_BV(8) | _BV(12),		  // 10
	_BV(9) | _BV(13),		  // 11
	_BV(10) | _BV(14),		  // 12
	_BV(11) | _BV(15),		  // 13
	_BV(12) | _BV(16),		  // 14
	_BV(13) | _BV(17),		  // 15
	_BV(14) | _BV(18),		  // 16
	_BV(15) | _BV(19),		  // 17
	_BV(16) | _BV(20),		  // 18
	_BV(17) | _BV(21),		  // 19
	_BV(18)					  // 20
};

const char *to_string(wl_status_t s)
{
	switch (s)
	{
	case WL_IDLE_STATUS:
		return "idle";
	case WL_NO_SSID_AVAIL:
		return "no SSID available";
	case WL_SCAN_COMPLETED:
		return "scan completed";
	case WL_CONNECTED:
		return "connected";
	case WL_CONNECT_FAILED:
		return "connect failed";
	case WL_CONNECTION_LOST:
		return "connection lost";
	case WL_WRONG_PASSWORD:
		return "wrong password";
	case WL_DISCONNECTED:
		return "disconnected";
	default:
		return "?";
	}
}

#define HHMMSS_FMT "%02u:%02u:%02u"
#define UTC2HHMMSS(x) uint8_t((x % 86400L) / 3600), uint8_t((x % 3600) / 60), uint8_t(x % 60)

#define HHMM_FMT "%02u:%02u"
#define UTC2HHMM(x) uint8_t((x / 60) % 24), uint8_t(x % 60)

String utc2hhmmss(unsigned long utc)
{
	String str = "";
	const int h = (utc % 86400L) / 3600;
	const int m = (utc % 3600) / 60;
	const int s = utc % 60;
	str += (h / 10) % 10 + '0';
	str += (h % 10) + '0';
	str += ':';
	str += (m / 10) % 10 + '0';
	str += (m % 10) + '0';
	str += ':';
	str += (s / 10) % 10 + '0';
	str += (s % 10) + '0';
	return str;
}

void setup()
{
	prefs.begin("corridorController");

	Serial.begin(115200);
#ifndef CAL_CPU_CYCLES
	// TODO: get from prefs
	Syslog.server = LOG_SRV_IPSTR;
	Syslog.app = LOG_SRV_APPSTR;
	Syslog.port = LOG_SRV_PORT;

	if (!WiFi.config(IPAddress(WIFI_ADDR_IP), IPAddress(WIFI_ADDR_GW), IPAddress(WIFI_ADDR_SM), IPAddress(1, 1, 1, 1), IPAddress(1, 0, 0, 1)))
	{
		Syslog.print("Wifi configuration error!");
	}

	String ssid = prefs.getString("WifiSSID", WIFI_DEFAULT_SSID);
	String pass = prefs.getString("WifiPassword", WIFI_DEFAULT_PASS);
	WiFi.setHostname(HOSTNAME);
	wifi_status_prev = WiFi.begin(ssid, pass);

	if (wifi_status_prev != WL_CONNECTED)
	{
		Syslog.print("\nWifi connecting...");
	}
	int wifi_start_count = 0;
	while ((wifi_status_prev = WiFi.status()) != WL_CONNECTED && wifi_start_count < WIFI_CONN_TIMEOUT)
	{
		if ((millis() % 1024) == 0)
		{
			Syslog.print('.');
		}
	}
	if (wifi_status_prev == WL_CONNECTED)
	{
		Syslog.print(" connected, IP address: ");
		Syslog.println(WiFi.localIP());
	}
	else
	{
		Syslog.printf("Wifi connection timeout, status: %s. Continuing boot...\n", to_string(wifi_status_prev));
	}

	Syslog.printf("Corridor Light Controller version %u build %lu\n", FW_VERSION, FW_BUILD);

	uint32_t counter = prefs.getULong("rebootCounter", 1); // default to 1
	Syslog.printf("Reboot count: %u\n", counter);
	counter++;
	prefs.putULong("rebootCounter", counter);

	uint32_t last_version = prefs.getULong("lastFirmwareVersion", 0);
	uint32_t last_build = prefs.getULong("lastFirmwareBuild", 0);
	if (last_version != FW_VERSION)
	{
		// put upgrade checks here for future upgrades
		Syslog.printf("Upgraded firmware from %u to %u\n", last_version, FW_VERSION);
		prefs.putULong("lastFirmwareVersion", FW_VERSION);
	}
	if (last_build != FW_BUILD)
	{
		Syslog.printf("Upgraded build from %u to %lu\n", last_build, FW_BUILD);
		prefs.putULong("lastFirmwareBuild", FW_BUILD);
	}

	for (uint8_t i = 0; i < CLC_SECT_NUM; i++)
	{
		if (!(pwm_avail[i] = pwm[i].begin(PWM_FREQ_PSC)))
		{
			Syslog.printf("PWM %u init error\n", i);
		}
		if (!(ioe_avail[i] = ioe[i].begin(PCA9554_BASE_ADDR + i)))
		{
			Syslog.printf("IOE %u init error\n", i);
		}
		if (!(adc_avail[i] = adc[i].begin(ADS7830_BASE_ADDR + i)))
		{
			Syslog.printf("ADC %u init error\n", i);
		}
		if (!(pwm_avail[i] && ioe_avail[i] && adc_avail[i]))
		{
			Syslog.printf("Cannot initialize distribution channel %u\n", i);
		}
	}
	Syslog.println("");

	ArduinoOTA.setHostname(HOSTNAME);
	ArduinoOTA.begin();
#endif
}

void loop()
{
#ifndef CAL_CPU_CYCLES
	// housekeeping
	wl_status_t wifi_status_curr = WiFi.status();
	if (wifi_status_curr != wifi_status_prev)
	{
		if (wifi_status_prev != WL_CONNECTED && wifi_status_curr == WL_CONNECTED)
		{
			Syslog.print(" connected, IP address: ");
			Syslog.println(WiFi.localIP());
		}
		else if (wifi_status_prev == WL_CONNECTED && wifi_status_curr != WL_CONNECTED)
		{
			Syslog.printf("WiFi disconnected (current status: %s), reconnecting %s...", to_string(wifi_status_curr), WiFi.reconnect() ? "started" : "error");
		}
		else
		{
			Syslog.printf("WiFi status changed (%s -> %s)\n", to_string(wifi_status_prev), to_string(wifi_status_curr));
		}
		wifi_status_prev = wifi_status_curr;
	}
	else if (wifi_status_curr != WL_CONNECTED)
	{
		if ((millis() % 1024) == 0)
		{
			Syslog.print('.');
		}
	}

	static unsigned long t_transit, t_daystart, t_dayend, t_sunrise, t_sunset, t_dawn, t_dusk;

	static bool time_updated = false;
	if (time_updated && timeClient.getHours() < TIME_UPDATE_HOUR)
	{
		Syslog.println("Time update pending");
		time_updated = false;
	}
	if ((!timeClient.isTimeSet() || (!time_updated && timeClient.getHours() >= TIME_UPDATE_HOUR)) && wifi_status_curr == WL_CONNECTED)
	{
		Syslog.println("Updating time...");
		if (timeClient.update())
		{
			time_updated = true;
			time_t utc = timeClient.getEpochTime();
			utc = CE.toLocal(utc);
			Syslog.printf("Time updated: %lu (" HHMMSS_FMT ")\n", utc, UTC2HHMMSS(utc));
			// perform sunrise/sunset calculations
			JulianDay jd = JulianDay(utc);
			double transit, daystart, dayend, sunrise, sunset, dawn, dusk;
			const double sun_altitude = 0.0353 * sqrt(POS_ALT);
			calcSunriseSunset(jd, POS_LAT, POS_LON, transit, daystart, dayend, CLC_DAYLIGHT_ALT - sun_altitude);
			calcSunriseSunset(jd, POS_LAT, POS_LON, transit, sunrise, sunset, SUNRISESET_STD_ALTITUDE - sun_altitude);
			calcSunriseSunset(jd, POS_LAT, POS_LON, transit, dawn, dusk, CLC_DAWNDUSK_ALT - sun_altitude);
			// const int t_conv = 60, t_ofs = 2;
			// t_transit = transit * t_conv + t_ofs;
			// t_sunrise = sunrise * t_conv + t_ofs;
			// t_sunset = sunset * t_conv + t_ofs;
			// t_dawn = dawn * t_conv + t_ofs;
			// t_dusk = dusk * t_conv + t_ofs;

			struct tm tm_midnight;
			gmtime_r(&utc, &tm_midnight);
			tm_midnight.tm_sec = 0;
			tm_midnight.tm_min = 0;
			tm_midnight.tm_hour = 0;
			utc = mktime(&tm_midnight);

			const int t_conv = 3600;
			t_transit = CE.toLocal(utc + transit * t_conv);
			t_daystart = CE.toLocal(utc + daystart * t_conv);
			t_dayend = CE.toLocal(utc + dayend * t_conv);
			t_sunrise = CE.toLocal(utc + sunrise * t_conv);
			t_sunset = CE.toLocal(utc + sunset * t_conv);
			t_dawn = CE.toLocal(utc + dawn * t_conv);
			t_dusk = CE.toLocal(utc + dusk * t_conv);

			Syslog.printf(
				"Sun times updated:\n"
				"\tDawn:           " HHMMSS_FMT "\n"  //%s\n"
				"\tSunrise:        " HHMMSS_FMT "\n"  //%s\n"
				"\tDaylight start: " HHMMSS_FMT "\n"  //%s\n"
				"\tTransit:        " HHMMSS_FMT "\n"  //%s\n"
				"\tDaylight end:   " HHMMSS_FMT "\n"  //%s\n"
				"\tSunset:         " HHMMSS_FMT "\n"  //%s\n"
				"\tDusk:           " HHMMSS_FMT "\n", //%s\n",
				UTC2HHMMSS(t_dawn),
				UTC2HHMMSS(t_sunrise),
				UTC2HHMMSS(t_daystart),
				UTC2HHMMSS(t_transit),
				UTC2HHMMSS(t_dayend),
				UTC2HHMMSS(t_sunset),
				UTC2HHMMSS(t_dusk));
		}
		else
		{
			Syslog.println("Timed out updating time");
		}
	}

	ArduinoOTA.handle();
	status_led.Update();
	// core functionality

	static uint16_t adc_vals_avg[CLC_TILE_NUM], adc_vals_acc[CLC_TILE_NUM] = {0};
	static uint16_t adc_avg_cnt, adc_avg_channels = 0;

	bool main_cycle_just_ran = false;
	static uint32_t prev_millis = 0;
	uint32_t curr_millis = millis(), dt_main_loop;
	if (curr_millis > CLC_MAIN_CYCLE_UPD_MS + prev_millis)
	{
		dt_main_loop = micros();
		main_cycle_just_ran = true;
		prev_millis = curr_millis;

		/******************************************************************************************************************************************
		Input section
		******************************************************************************************************************************************/

		uint8_t adc_vals[CLC_TILE_NUM];
		uint8_t io_state[CLC_SECT_NUM];
		static uint8_t prev_io_state[CLC_SECT_NUM];
		static bool first_io_read[CLC_SECT_NUM] = {true};
		uint32_t motion_state = 0;
		adc_avg_channels = 0;
		// static uint32_t prev_motion = 0;
		//  read all inputs
		//  time: 24*2*8*2.5us = ~1ms
		//  NOTE: at 10ms update that's already 10% of the time
		for (uint8_t i = 0; i < CLC_SECT_NUM; i++)
		{
			io_state[i] = (1 << CLC_BTN_CHAN);
			if (ioe_avail[i])
			{
				io_state[i] = ioe[i].input_port_reg->read(); // NOTE: this would be a private method, but it's needed to avoid 8 reads instead of one
			}
			// io_state[i] = 0xFF;
			if (first_io_read[i])
			{
				first_io_read[i] = false;
				prev_io_state[i] = io_state[i];
			}
			motion_state |= ((uint32_t)(io_state[i] & ~(1 << CLC_BTN_CHAN))) << (i * CLC_CHAN_NUM);
			for (uint8_t j = 0; j < CLC_CHAN_NUM; j++)
			{
				uint8_t k = i * CLC_CHAN_NUM + j;
				adc_vals[k] = 0x00;
				if (adc_avail[i])
				{
					adc_vals[k] = adc[i].readADCsingle(j, INTERNAL_REF_OFF_ADC_ON);
					adc_avg_channels += adc_vals[k];
				}
			}
		}
		adc_avg_channels /= CLC_TILE_NUM;

		// ADC long averaging
		for (uint8_t i = 0; i < CLC_TILE_NUM; i++)
		{
			if (adc_avg_cnt >= CLC_ADC_SAMPLE_COUNT - 1)
			{
				adc_vals_avg[i] = adc_vals_acc[i] + adc_vals[i];
				adc_vals_acc[i] = 0;
			}
			else
			{
				adc_vals_acc[i] += adc_vals[i];
			}
		}
		if (adc_avg_cnt >= CLC_ADC_SAMPLE_COUNT)
		{
			adc_avg_cnt = 0;
		}
		else
		{
			++adc_avg_cnt;
		}

		// button logic
		static uint8_t press_count[CLC_SECT_NUM] = {0};
		// static bool button_held[CLC_DIST_NUM] = {false};
		uint8_t button_press[CLC_SECT_NUM] = {button_press_none};
		static uint8_t sect_mode[CLC_SECT_NUM], prev_sect_mode[CLC_SECT_NUM] = {sect_mode_auto};
		for (uint8_t i = 0; i < CLC_SECT_NUM; i++)
		{
			prev_sect_mode[i] = sect_mode[i];
			bool button_pressed = !(io_state[i] & (1 << CLC_BTN_CHAN));
			if (button_pressed)
			{
				press_count[i]++;
				if (press_count[i] == CLC_BTN_LONG_PRESS_TICKS) // equality comparison to trigger only once
				{
					button_press[i] = button_press_long;
					sect_mode[i] = sect_mode_fullon;
					Syslog.printf("Button %u long press\n", i);
				}
			}
			else
			{
				if (press_count[i] >= CLC_BTN_SHORT_PRESS_TICKS && press_count[i] < CLC_BTN_LONG_PRESS_TICKS)
				{
					button_press[i] = button_press_short;
					if (sect_mode[i] == sect_mode_auto)
					{
						sect_mode[i] = sect_mode_manual;
					}
					else
					{
						sect_mode[i] = sect_mode_auto;
					}
					Syslog.printf("Button %u short press\n", i);
				}
				press_count[i] = 0;
			}
		}

		// motion detection
		static uint16_t motion_ticks[CLC_TILE_NUM] = {0};
		for (uint8_t i = 0; i < CLC_SECT_NUM; i++)
		{
			for (uint8_t j = 0; j < CLC_CHAN_NUM; j++)
			{
				uint8_t k = i * CLC_CHAN_NUM + j;
				bool moved = motion_state & (1 << k);
				if (moved)
				{
					motion_ticks[i] = CLC_MOTION_TICKS;
				}
				else if (motion_ticks[i] != 0)
				{
					--motion_ticks[i];
				} // else // -> motion_ticks is already 0
			}
		}

		/******************************************************************************************************************************************
		Main section
		******************************************************************************************************************************************/

		// use some memory in favor of update speed since writing all via i2c could take some time
		static uint16_t prev_led_cold[CLC_TILE_NUM], prev_led_warm[CLC_TILE_NUM], target_led_cold[CLC_TILE_NUM], target_led_warm[CLC_TILE_NUM] = {0};
		uint16_t curr_led_cold[CLC_TILE_NUM], curr_led_warm[CLC_TILE_NUM];
		static uint8_t light_state[CLC_TILE_NUM] = {light_state_off};

		for (uint8_t k = 0; k < CLC_TILE_NUM; k++){
				// start value
				curr_led_cold[k] = prev_led_cold[k];
				curr_led_warm[k] = prev_led_warm[k];

				uint8_t next_state = light_state[k];

				// concentrate all state transitions here?
				if (sect_mode[i] == sect_mode_fullon)
				{
					target_led_cold[k] = CLC_PWM_MAX;
					target_led_warm[k] = CLC_PWM_MAX;
					next_state = light_state_on;
				}
				else if (sect_mode[i] == sect_mode_manual)
				{
					if (prev_sect_mode[i] != sect_mode_manual)
					{
						if (light_state[k] == light_state_off)
						{
							next_state = light_state_fade_in;
						}
					}
				}
				else // auto mode
				{
					if (motion_ticks[i])
					{
						next_state = light_state_fade_in;
					}
					else
					{
						next_state = light_state_fade_out;
					}
				}

				// concentrate all fades here?
				switch (light_state[i])
				{
				case light_state_off:
				{

					break;
				}
				case light_state_fade_in:
				{

					break;
				}
				case light_state_on:
				{

					break;
				}
				case light_state_fade_out:
				{

					break;
				}
				default:
					break;
				}

				uint32_t direct_neighbors_mask, indirect_neighbors_mask = 0;

				for (uint8_t h = 0; h < CLC_TILE_NUM; h++)
				{
					if (clc_direct_neighbors[k])
				{
					/* code */
				}
				}
				
				
				

			
		}

		/******************************************************************************************************************************************
		Output section
		******************************************************************************************************************************************/

		// set all LEDs
		// worst case time: 21 * 5 * 8 * 2.5us = 2.1ms
		for (uint8_t i = 0; i < CLC_SECT_NUM; i++)
		{
			for (uint8_t j = 0; j < CLC_CHAN_NUM; j++)
			{
				uint8_t k = i * CLC_CHAN_NUM + j;
				if (curr_led_cold[k] != prev_led_cold[k])
				{
					if (pwm_avail[i])
					{
						pwm[i].setPin(j * 2, curr_led_cold[k] > CLC_PWM_MIN ? curr_led_cold[k] : 0); // even = cold
					}
					prev_led_cold[k] = curr_led_cold[k];
				}
				if (curr_led_warm[k] != prev_led_warm[k])
				{
					if (pwm_avail[i])
					{
						pwm[i].setPin(1 + j * 2, curr_led_warm[k] > CLC_PWM_MIN ? curr_led_warm[k] : 0); // odd = warm
					}
					prev_led_warm[k] = curr_led_warm[k];
				}
			}
		}

		for (uint8_t i = 0; i < CLC_SECT_NUM; i++)
		{
			prev_io_state[i] = io_state[i];
		}
		// prev_motion = motion_state;

		dt_main_loop = micros() - dt_main_loop;
	}

	// slower code to execute in "spare time"
	static uint32_t sec_prev_millis = 0;
	uint32_t sec_curr_millis = millis();
	if (main_cycle_just_ran && sec_curr_millis > CLC_SLOW_CYCLE_UPD_MS + sec_prev_millis && dt_main_loop < (CLC_MAIN_CYCLE_UPD_MS * 1000 / 2))
	{
		Serial.printf("Secondary cycle, primary delta: %u\n", dt_main_loop);
		sec_prev_millis = sec_curr_millis;
	}
#endif

#ifdef CAL_CPU_CYCLES
	static uint32_t cal_cyc, cal_us = 0;
	// cal_cyc -= micros();
	uint32_t tmp_us = micros();
#endif
	static volatile uint32_t cpu_cycles = 0;
	cpu_cycles++;
	static uint32_t cpu_millis_prev = 0;
	uint32_t cpu_millis_curr = millis();
	if (cpu_millis_curr > CPU_LOAD_REPORT_MS + cpu_millis_prev)
	{
#ifdef CAL_CPU_CYCLES
		cal_cyc = (cal_us * (F_CPU / 1e6)) / cal_cyc;
		Serial.printf("Cycles: %u ", cal_cyc);
		cal_cyc = 0;
		cal_us = 0;
#else
#endif
		uint32_t load = 100 - (100 * CPU_LOAD_OVERHEAD) * cpu_cycles / CPU_CYC_EXP;
		Serial.printf(">CPU:%u%%\r\n", load);
		cpu_millis_prev = cpu_millis_curr;
		cpu_cycles = 0;
	}
#ifdef CAL_CPU_CYCLES
	else
	{
		// cal_cyc += micros();
		cal_us += micros() - tmp_us;
		cal_cyc++;
		// Serial.println(micros() - tmp_cyc);
	}
#endif
}
