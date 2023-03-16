/*
  FM Synthesizer Core
  Written for Arduino Uno R3
*/
#define eightBitA 0B10000001
#define eightBitB 0B00001001
 
#define nineBitA
#define nineBitB

#define tenBitA
#define tenBitB

void setup() {
  // put your setup code here, to run once:
  noInterrupts();

  pinMode(9, OUTPUT);//For now output PWM on pin 9

  /*
    TCCR1A & TCCR1B: Timer/Counter Control Registers A & B respectively.
    I am setting Timer 1 to use the 9-bit fast PWM mode for output.
    This means the timer will cycle every 512 clock pulses. 
    With the Arduino running at 16MHz, that gives a sample rate of
    31.25kHz, which is plenty. I could perhaps go 8 bit but 9 bits is nice
  */
  TCCR1A = 0B10000010;
  TCCR1B = 
}

void loop() {
  // put your main code here, to run repeatedly:

}
