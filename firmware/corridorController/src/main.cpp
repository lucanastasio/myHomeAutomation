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

Preferences prefs;

wl_status_t wifi_status_prev;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "it.pool.ntp.org", 7200); // unneeded time interval, update managed directly
#define TIME_UPDATE_HOUR 3							   // a what time the time should be updated

#define FW_VERSION 0U
#define FW_BUILD PIO_COMPILE_TIME
#define CPU_CYCLES_MULT 4

#define WIFI_CONN_TIMEOUT 60 // seconds
#define HOSTNAME "corridorLightController"

#define PCA9685_BASE_ADDR 0x40
#define PCA9554_BASE_ADDR 0x38
#define ADS7830_BASE_ADDR 0x48

#define PWM_FREQ_PSC 3

#define CLC_UPD_MS 10							   // update interval in milliseconds, 10ms -> 100Hz
#define CLC_DIST_NUM 3							   // number of distribution boards
#define CLC_CHAN_NUM 7							   // number of actually used channels per distribution board
#define CLC_TILE_NUM (CLC_DIST_NUM * CLC_CHAN_NUM) // total number of tiles
#define CLC_BTN_CHAN 7							   // channel of the I/O expander used as the button input
// single press = on with auto brightness and timeout, long press = full on, second press = off (auto)
#define CLC_BTN_SHORT_PRESS_TICKS 2	 // number of CLC_UPD_MS ticks to consider a press as a short one (debouncing)
#define CLC_BTN_LONG_PRESS_TICKS 100 // number of CLC_UPD_MS ticks to consider a press as a long one
// for analog control:
// pwm is scaled down and filtered, 100% gives 2.5V while the minimum is 0.3V corresponding to 12%, in 12 bits it's 491
#define CLC_PWM_MIN 491

#define CLC_DAWNDUSK_ALT CIVIL_DAWNDUSK_STD_ALTITUDE

enum
{
	button_press_none,
	button_press_short,
	button_press_long
};

Adafruit_PWMServoDriver pwm[CLC_DIST_NUM] = {
	Adafruit_PWMServoDriver(PCA9685_BASE_ADDR + 0),
	Adafruit_PWMServoDriver(PCA9685_BASE_ADDR + 1),
	Adafruit_PWMServoDriver(PCA9685_BASE_ADDR + 2)};

Adafruit_XCA9554 ioe[CLC_DIST_NUM] = {
	Adafruit_XCA9554()};

Adafruit_ADS7830 adc[CLC_DIST_NUM] = {
	Adafruit_ADS7830()};

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
	Serial.begin(115200);
	Serial.printf("Corridor Light Controller version %u build %lu\n", FW_VERSION, FW_BUILD);

	prefs.begin("corridorController");
	uint32_t counter = prefs.getULong("rebootCounter", 1); // default to 1
	Serial.printf("Reboot count: %u\n", counter);
	counter++;
	prefs.putULong("rebootCounter", counter);

	uint32_t last_version = prefs.getULong("lastFirmwareVersion", 0);
	uint32_t last_build = prefs.getULong("lastFirmwareBuild", 0);
	if (last_version != FW_VERSION)
	{
		// put upgrade checks here for future upgrades
		Serial.printf("Upgraded firmware from %u to %u\n", last_version, FW_VERSION);
		prefs.putULong("lastFirmwareVersion", FW_VERSION);
	}
	if (last_build != FW_BUILD)
	{
		Serial.printf("Upgraded build from %u to %lu\n", last_build, FW_BUILD);
		prefs.putULong("lastFirmwareBuild", FW_BUILD);
	}

	// Rome: 41.898209742090536, 12.475697353988663

	for (uint8_t i = 0; i < CLC_DIST_NUM; i++)
	{
		if (!pwm[i].begin(PWM_FREQ_PSC))
		{
			Serial.printf("PWM %u init error\n", i);
		}
		if (!ioe[i].begin(PCA9554_BASE_ADDR + i))
		{
			Serial.printf("IOE %u init error\n", i);
		}
		if (!adc[i].begin(ADS7830_BASE_ADDR + i))
		{
			Serial.printf("ADC %u init error\n", i);
		}
	}
	Serial.println("");

	if (!WiFi.config(IPAddress(WIFI_ADDR_IP), IPAddress(WIFI_ADDR_GW), IPAddress(WIFI_ADDR_SM), IPAddress(1, 1, 1, 1), IPAddress(1, 0, 0, 1)))
	{
		// while (true)
		//{
		Serial.print("Wifi configuration error!");
		// delay(1000);
		//}
	}

	String ssid = prefs.getString("WifiSSID", WIFI_DEFAULT_SSID);
	String pass = prefs.getString("WifiPassword", WIFI_DEFAULT_PASS);
	WiFi.setHostname(HOSTNAME);
	wifi_status_prev = WiFi.begin(ssid, pass);

	if (wifi_status_prev != WL_CONNECTED)
	{
		Serial.print("Wifi connecting...");
	}
	ArduinoOTA.setHostname(HOSTNAME);
	ArduinoOTA.begin();
}

void loop()
{
	// housekeeping
	wl_status_t wifi_status_curr = WiFi.status();
	if (wifi_status_curr != wifi_status_prev)
	{
		if (wifi_status_prev != WL_CONNECTED && wifi_status_curr == WL_CONNECTED)
		{
			Serial.print(" connected, IP address: ");
			Serial.println(WiFi.localIP());
			if (!ArduinoOTA.getHostname().length())
			{
				// ArduinoOTA.setHostname(HOSTNAME);
				// ArduinoOTA.begin();
			}
		}
		else if (wifi_status_prev == WL_CONNECTED && wifi_status_curr != WL_CONNECTED)
		{
			Serial.printf("WiFi disconnected (current status: %s), reconnecting %s...", to_string(wifi_status_curr), WiFi.reconnect() ? "started" : "error");
		}
		else
		{
			Serial.printf("WiFi status changed (%s -> %s)\n", to_string(wifi_status_prev), to_string(wifi_status_curr));
		}
		wifi_status_prev = wifi_status_curr;
	}
	else if (wifi_status_curr != WL_CONNECTED)
	{
		// Serial.print('.');
	}

	static unsigned long t_transit, t_sunrise, t_sunset, t_dawn, t_dusk;

	static bool time_updated = false;
	if (time_updated && timeClient.getHours() < TIME_UPDATE_HOUR)
	{
		Serial.println("Time update pending");
		time_updated = false;
	}
	if ((!timeClient.isTimeSet() || (!time_updated && timeClient.getHours() >= TIME_UPDATE_HOUR)) && wifi_status_curr == WL_CONNECTED)
	{
		Serial.println("Updating time...");
		if (timeClient.update())
		{
			time_updated = true;
			unsigned long utc = timeClient.getEpochTime();
			Serial.printf("Time updated: %lu (" HHMMSS_FMT ")\n", utc, UTC2HHMMSS(utc));
			// perform sunrise/sunset calculations
			JulianDay jd = JulianDay(utc);
			double transit, sunrise, sunset, dawn, dusk;
			const double sun_altitude = 0.0353 * sqrt(POS_ALT);
			calcSunriseSunset(jd, POS_LAT, POS_LON, transit, sunrise, sunset, SUNRISESET_STD_ALTITUDE - sun_altitude);
			calcSunriseSunset(jd, POS_LAT, POS_LON, transit, dawn, dusk, CLC_DAWNDUSK_ALT - sun_altitude);
			const int t_conv = 60, t_ofs = 2;
			t_transit = transit * t_conv + t_ofs;
			t_sunrise = sunrise * t_conv + t_ofs;
			t_sunset = sunset * t_conv + t_ofs;
			t_dawn = dawn * t_conv + t_ofs;
			t_dusk = dusk * t_conv + t_ofs;

			Serial.printf(
				"Sun times updated:\n"
				"\tDawn:    " HHMM_FMT "\n"	 //%s\n"
				"\tSunrise: " HHMM_FMT "\n"	 //%s\n"
				"\tTransit: " HHMM_FMT "\n"	 //%s\n"
				"\tSunset:  " HHMM_FMT "\n"	 //%s\n"
				"\tDusk:    " HHMM_FMT "\n", //%s\n",
				UTC2HHMM(t_dawn),
				UTC2HHMM(t_sunrise),
				UTC2HHMM(t_transit),
				UTC2HHMM(t_sunset),
				UTC2HHMM(t_dusk));
		}
		else
		{
			Serial.println("Timed out updating time");
		}
	}

	ArduinoOTA.handle();
	status_led.Update();
	// core functionality

	bool main_cycle_just_ran = false;
	static uint32_t prev_millis = 0;
	uint32_t curr_millis = millis(), dt_main_loop;
	if (curr_millis - CLC_UPD_MS > prev_millis)
	{
		dt_main_loop = micros();
		main_cycle_just_ran = true;
		prev_millis = curr_millis;
		uint8_t adc_vals[CLC_TILE_NUM];
		uint8_t io_state[CLC_DIST_NUM];
		static uint8_t prev_io_state[CLC_DIST_NUM];
		static bool first_io_read[CLC_DIST_NUM] = {true};
		// read all inputs
		// time: 24*2*8*2.5us = ~1ms
		// NOTE: at 10ms update that's already 10% of the time
		for (uint8_t i = 0; i < CLC_DIST_NUM; i++)
		{
			// io_state[i] = ioe[i].input_port_reg->read(); // NOTE: this would be a private method, but it's needed to avoid 8 reads instead of one
			io_state[i] = 0xFF;
			if (first_io_read[i])
			{
				first_io_read[i] = false;
				prev_io_state[i] = io_state[i];
			}
			for (uint8_t j = 0; j < CLC_CHAN_NUM; j++)
			{
				// adc_vals[i * j] = adc[i].readADCsingle(j, INTERNAL_REF_OFF_ADC_ON);
				adc_vals[i * j] = 0x00;
			}
		}

		// button logic
		static uint8_t press_count[CLC_DIST_NUM] = {0};
		// static bool button_held[CLC_DIST_NUM] = {false};
		uint8_t button_press[CLC_DIST_NUM] = {button_press_none};
		for (uint8_t i = 0; i < CLC_DIST_NUM; i++)
		{
			bool button_pressed = !(io_state[i] & (1 << CLC_BTN_CHAN));
			if (button_pressed)
			{
				press_count[i]++;
				if (press_count[i] == CLC_BTN_LONG_PRESS_TICKS) // equality comparison to trigger only once
				{
					button_press[i] = button_press_long;
					Serial.printf("Button %u long press\n", i);
				}
			}
			else
			{
				if (press_count[i] >= CLC_BTN_SHORT_PRESS_TICKS && press_count[i] < CLC_BTN_LONG_PRESS_TICKS)
				{
					button_press[i] = button_press_short;
					Serial.printf("Button %u short press\n", i);
				}
				press_count[i] = 0;
			}
		}

		// use some memory in favor of update speed since writing all via i2c could take some time
		static uint16_t prev_led_cold[CLC_TILE_NUM], prev_led_warm[CLC_TILE_NUM] = {0};
		uint16_t curr_led_cold[CLC_TILE_NUM], curr_led_warm[CLC_TILE_NUM];
		for (uint8_t i = 0; i < CLC_DIST_NUM; i++)
		{
			for (uint8_t j = 0; j < CLC_CHAN_NUM; j++)
			{
				uint8_t tile_index = i * j;
				// start value
				curr_led_cold[tile_index] = prev_led_cold[tile_index];
				curr_led_warm[tile_index] = prev_led_warm[tile_index];
			}
		}

		// set all LEDs
		// worst case time: 21 * 5 * 8 * 2.5us = 2.1ms
		for (uint8_t i = 0; i < CLC_DIST_NUM; i++)
		{
			for (uint8_t j = 0; j < CLC_CHAN_NUM; j++)
			{
				uint8_t tile_index = i * j;
				if (curr_led_cold[tile_index] != prev_led_cold[tile_index])
				{
					// pwm[i].setPin(j * 2, curr_led_cold[tile_index]); // even = cold
					prev_led_cold[tile_index] = curr_led_cold[tile_index];
				}
				if (curr_led_warm[tile_index] != prev_led_warm[tile_index])
				{
					// pwm[i].setPin(1 + j * 2, curr_led_warm[tile_index]); // odd = warm
					prev_led_warm[tile_index] = curr_led_warm[tile_index];
				}
			}
		}
		dt_main_loop = micros() - dt_main_loop;
	}

	// slower code to execute in "spare time"
	if (main_cycle_just_ran)
	{

	}

	static uint32_t cpu_cycles;
	cpu_cycles++;
	if (cpu_cycles >= F_CPU * CPU_CYCLES_MULT)
	{
		static uint32_t cpu_millis;
		// TODO float load =
		// Serial.printf("CPU load: %d%%\n", load);
		cpu_millis = curr_millis;
	}
}
