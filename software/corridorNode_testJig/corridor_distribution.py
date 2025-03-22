# chips and base addresses
# PWM: PCA9685, IO: TCA9554(A?),    ADC: ADS7830
# 0x40          0x38                0x48

import os
from adafruit_extended_bus import ExtendedI2C
from adafruit_pca9685 import PCA9685
from adafruit_pca9554 import PCA9554
from adafruit_ads7830.ads7830 import ADS7830


def find_i2c_bus(adapter_keyword):
	i2c_path = "/sys/class/i2c-dev/"
	try:
		for entry in os.listdir(i2c_path):
			adapter_name_path = os.path.join(i2c_path, entry, "device/name")
			if os.path.exists(adapter_name_path):
				with open(adapter_name_path, "r") as f:
					adapter_name = f.read().strip()
					if adapter_keyword in adapter_name:
						return int(entry.replace("i2c-", ""))
	except Exception as e:
		raise RuntimeError(f"Error finding I²C bus: {e}")
	return -1


class CorridorDist:
	PCA9685_BASE_ADDR = 0x40
	PCA9554_BASE_ADDR = 0x38
	ADS7830_BASE_ADDR = 0x48
	
	def __init__(self, bus=None, addr=0x0):
		i2c = ExtendedI2C(bus or find_i2c_bus("CP2112"))
		self.pwm = PCA9685(i2c, address=self.PCA9685_BASE_ADDR+addr)
		self.exp = PCA9554(i2c, address=self.PCA9554_BASE_ADDR+addr)
		self.adc = ADS7830(i2c, address=self.ADS7830_BASE_ADDR+addr, int_ref_power_down=True)
		self.pwm.frequency = 1526
		
	def set_address(self, addr):
		self.pwm.i2c_device.device_address = self.PCA9685_BASE_ADDR + addr
		self.exp.i2c_device.device_address = self.PCA9554_BASE_ADDR + addr
		self.adc.i2c_device.device_address = self.ADS7830_BASE_ADDR + addr
		
	def get_light(self):
		return [(self.adc.read(i) >> 8) for i in range(8)]

	def get_motion(self):
		d = [1-int(bit) for bit in f"{self.exp.read_gpio(0x00):08b}"]
		d.reverse()
		return d
	
	# even = cold, odd = warm
	def set_leds(self, cold, warm):
		for i in range(8):
			self.pwm.channels[2 * i].duty_cycle = cold[i] << 4
			self.pwm.channels[1 + 2 * i].duty_cycle = warm[i] << 4
		


if __name__ == '__main__':
	# dist = [CorridorDist(addr=i) for i in range(3)]
	dist = CorridorDist(addr=2)
	