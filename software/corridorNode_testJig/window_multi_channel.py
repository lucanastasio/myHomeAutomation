from ui_multi_channel import Ui_MainWindow

from PyQt5.QtWidgets import QApplication, QMainWindow, QSpinBox, QSlider, QProgressBar
from PyQt5.QtCore import QTimer
from corridor_distribution import CorridorDist

class Window(QMainWindow, Ui_MainWindow):
	def __init__(self, parent=None):
		super().__init__(parent)
		self.setupUi(self)
		self.wled_sliders: list[QSlider] = [self.__getattribute__(f'WLED_slider_{i}') for i in range(8)]
		self.wled_spinboxes: list[QSpinBox] = [self.__getattribute__(f'WLED_spinBox_{i}') for i in range(8)]
		self.cled_sliders: list[QSlider] = [self.__getattribute__(f'CLED_slider_{i}') for i in range(8)]
		self.cled_spinboxes: list[QSpinBox] = [self.__getattribute__(f'CLED_spinBox_{i}') for i in range(8)]
		self.light_bars: list[QProgressBar] = [self.__getattribute__(f'light_progressBar_{i}') for i in range(8)]
		self.motion_bars: list[QProgressBar] = [self.__getattribute__(f'motion_progressBar_{i}') for i in range(8)]
		self.timer = QTimer()
		self.connectSignalsSlots()
		self.timer.start(20)
		for i in range(4):
			try:
				self.dist = CorridorDist(addr=i)
				self.address_spinBox.setValue(i)
				break
			except Exception as e:
				if "[Errno 110]" in str(e) and i < 3:
					print("Trying next address")
				else:
					exit("Not found")
					pass
	
	def connectSignalsSlots(self):
		self.timer.timeout.connect(self.timerUpdate)
		for i in range(8):
			self.wled_spinboxes[i].valueChanged.connect(self.wled_sliders[i].setValue)
			self.wled_sliders[i].valueChanged.connect(self.wled_spinboxes[i].setValue)
			self.cled_spinboxes[i].valueChanged.connect(self.cled_sliders[i].setValue)
			self.cled_sliders[i].valueChanged.connect(self.cled_spinboxes[i].setValue)
			
	def timerUpdate(self):
		try:
			# self.dist.set_address(self.address_spinBox.value())
			light = self.dist.get_light()
			motion = self.dist.get_motion()
			for i in range(8):
				self.light_bars[i].setValue(light[i])
				self.motion_bars[i].setValue(motion[i])
			cold = [self.cled_sliders[i].value() for i in range(8)]
			warm = [self.wled_sliders[i].value() for i in range(8)]
			self.dist.set_leds(cold, warm)
			self.status_label.setText("OK")
		except Exception as e:
			if "[Errno 110]" in str(e):
				self.status_label.setText("No dev")
			else:
				self.status_label.setText("Error")
				print(e)
			
