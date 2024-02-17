//#include <CRC.h>

#define ICR1MAX 10500
#define OCRSCALE (ICR1MAX / 100)
#define PWMMIN 1260
#define TXINTERVAL 40

void setup() {
	Serial.begin(115200);
	pinMode(8, INPUT_PULLUP);

	// Timer 1 setup, Mode-14 Fast, Top=ICR
	TCCR1B = 0x18;	 // 00011000
	TCCR1A = 0xA2;	 // 10100010
	ICR1 = ICR1MAX;	 // 1526 Hz
	OCR1A = 0;
	OCR1B = 0;
	TCNT1 = 0x0;
	pinMode(9, OUTPUT);	  // OC1a
	pinMode(10, OUTPUT);  // OC1b
	TCCR1B |= 1;		  // Prescale=1, Enable Timer
}

void loop() {
	static uint32_t ms;
	if (millis() > ms + TXINTERVAL) {
		uint8_t l = highByte(analogRead(A0));
		uint8_t m = 255 * digitalRead(8);
		Serial.print("L");
		Serial.print(l);
		Serial.print(",M");
		Serial.print(m);
		Serial.println(",U255");
		ms = millis();
	}
	if (Serial.available()) {
		char c = Serial.read();
		if (c == 'c') {
			uint8_t v = Serial.parseInt();
			OCR1A = OCRSCALE * v;
		} else if (c == 'w') {
			uint8_t v = Serial.parseInt();
			OCR1B = OCRSCALE * v;
		}
	}
}
