/*
  FM Synthesizer Core
  Written for Arduino Uno R3
*/

//Macros for selecting timer bit resolution
#define eightBit 0B10000001 
#define nineBit  0B10000010
#define tenBit   0B10000011
/*
  Instead of attempting to generate the sine waves arithematically,
  generate a wavetable at startup to use instead. Also easier than
  hardcoding the table.
*/
const float pi = 3.14159265;
char sine[256]; //Sine wavetable consists of 256 samples.
void generateSineTable(){
  for(int i = 0; i< 256; i++){
    sine[i] = (sin(2 * pi * (i + 0.5) / 256)) * 128; 
  }
}

/*
  Similar to generateSineTable(), this generates the index increments for each
  note. The timer will increment the index of the sine table every time it 
  cycles. For the 9-bit timer this is once every 512 clock cycles. The 65530
  value represents the number of possible values of an unsigned int.
*/
#define clockSpeed 16000000.0

unsigned int noteIncHigh[48]; //Higher octaves
unsigned int noteIncHiMid[48]; //Upper-Mid octaves
unsigned int noteIncLoMid[48]; //Lower-Mid octaves
unsigned int noteIncBass[48]; //Bass octaves
void generateNoteIncrements(int bits){
    for(int i = 0; i < 48; i++){
      noteIncHigh[i] = 440.0 * pow(2.0, ((i - 9)/12.0)) * 65536.0 / 
      (clockSpeed/bits) + 0.5;
    }
    for(int i = 0; i < 48; i++){
      noteIncHiMid[i] = 440.0 * pow(2.0, ((i - 21)/12.0)) * 65536.0 / 
      (clockSpeed/bits) + 0.5;
    }
    for(int i = 0; i < 48; i++){
      noteIncLoMid[i] = 440.0 * pow(2.0, ((i - 33)/12.0)) * 65536.0 / 
      (clockSpeed/bits) + 0.5;
    }
    for(int i = 0; i < 48; i++){
      noteIncBass[i] = 440.0 * pow(2.0, ((i - 45)/12.0)) * 65536.0 / 
      (clockSpeed/bits) + 0.5;
    }
}
//Macros for use when generating note increments
#define eightBits 256
#define nineBits  512
#define tenBits   1024

#define bass 0
#define loMid 1
#define hiMid 2
#define high 3

byte inputStatus = 0;
byte prevInputStatus = 0;

void setup() {
  // put your setup code here, to run once:
  noInterrupts();

  generateSineTable();

  generateNoteIncrements(nineBits); //9-bit resolution as default

  pinMode(9, OUTPUT);//Pn 9 is the one connected to Timer1's PWM output.
  /*
    TCCR1A & TCCR1B: Timer/Counter Control Registers A & B respectively.
    I am setting Timer 1 to use the 9-bit fast PWM mode for output.
    This means the timer will cycle every 512 clock pulses. 
    With the Arduino running at 16MHz, that gives a sample rate of
    31.25kHz, which is plenty. I could perhaps go 8 bit but 9 bits is nice

    Fast PWM 8-Bit:
      TCCR1A = 0B10000001
      TCCR1B = 0B00001001
    Fast PWM 9-Bit:
      TCCR1A = 0B10000010
      TCCR1B = 0B00001001
    Fast PWM 10-Bit
      TCCR1A = 0B10000011
      TCCR1B = 0B00001001
  */
  TCCR1A = nineBit;
  TCCR1B = 0B00001001; //Same for all 3 modes
  //Only use Port D pins for input from controller
  DDRD = 0B00000000; //Set all pins of Port D to input
  PORTD = 0B11111111; //Set all pins high for pullup input. 
  inputStatus = PIND; //All musical input comes from PIND
  //prevInputStatus = inputStatus; 
  //Initialize prevInputStatus so nothing happens on startup. May remove later.
}

/*
  Handles both timing and modulation. Function is forces inline for runtime 
  efficiency. Ensures that samples occur every 512 clock cycles regardless of
  when the function was called in between those samples. It does this by 
  waiting until the timer overflows to update the waveform. The timer overflows
  after every sample, so it should be timed perfectly this way.

  Most calculations are done using ints even though the sine wavetable only
  has 256 phases. The "compression" of 16 bit integers into byte values is
  handled by the right shift operations in the final lines.
*/
unsigned int carrierPhaseIdx[4] = {0,0,0,0};
int carrierPhaseInc[4] = {0,0,0,0};
byte carrierAmplitude[4] = {0,0,0,0};

unsigned int FMPhaseIdx[4] = {0,0,0,0};
unsigned int FMPhaseInc[4] = {0,0,0,0};
unsigned int FMAmplitude[4] = {0,0,0,0};

inline void updatePulseWidth() __attribute__((always_inline));
inline void updatePulseWidth() {

  //Wait until timer overflows then clear overflow bit.
  while((TIFR1 & 0B00000001) != 1);
  TIFR1 |= 0B00000001; 
  //Bit 0 of TIFR1 is the timer overflow bit.
  //For some reason it is cleared by writing a 1 to it. 
  //I may experiment with putting the waiting section after the calculations
  //but before the assignment of OCR1A

  //Update the phase index of each modulator
  FMPhaseIdx[0] += FMPhaseInc[0];
  FMPhaseIdx[1] += FMPhaseInc[1];
  FMPhaseIdx[2] += FMPhaseInc[2];
  FMPhaseIdx[3] += FMPhaseInc[3];

  //Update the phase index of each carrier
  carrierPhaseIdx[0] += carrierPhaseInc[0];
  carrierPhaseIdx[1] += carrierPhaseInc[1];
  carrierPhaseIdx[2] += carrierPhaseInc[2];
  carrierPhaseIdx[3] += carrierPhaseInc[3];

  int pulseWidth;
  //The '>> 8' is a very quick and dirty way to roughly divide by 256.
  //Allows me to use ints to index an array with only a byte's worth of values.
  pulseWidth = sine[(carrierPhaseIdx[0] + sine[FMPhaseIdx[0] >> 8] * 
  FMAmplitude[0]) >> 8] * carrierAmplitude[0];

  pulseWidth += sine[(carrierPhaseIdx[1] + sine[FMPhaseIdx[1] >> 8] * 
  FMAmplitude[1]) >> 8] * carrierAmplitude[1];

  pulseWidth += sine[(carrierPhaseIdx[2] + sine[FMPhaseIdx[2] >> 8] * 
  FMAmplitude[2]) >> 8] * carrierAmplitude[2];

  pulseWidth += sine[(carrierPhaseIdx[3] + sine[FMPhaseIdx[3] >> 8] * 
  FMAmplitude[3]) >> 8] * carrierAmplitude[3];

  OCR1A = pulseWidth/128 + 256;
}

#define idle 255
#define doNothing 0B00000011
#define noteOn 0B00000010
#define noteOff 0B00000001
#define config 0B00000000
byte controlMask = 0B11000000;
byte octMask = 0B00110000;
byte noteMask = 0B00001111;

/*
  ADSR: Attack, Decay, Sustain, Release
  Attempts to simulate realistic sound decay over time.
  Amplitude over time:

    /\__
   /    \

  | /|\|__| |
  |/ | |  |\|
   1  2  3  4
  1 - Slope of first upward motion determined by attack parameter.
  2 - Slope of first downward motion determined by decay parameter.
  3 - Level of plateau determined by sustain parameter.
  4 - Slope of final downward motion determined by release parameter.
*/
unsigned int loudness = 64; //Overall loudness
//ADSR for carrier amplitude
unsigned int carrierAttack = 4096;
unsigned int carrierDecay = 1024;
unsigned int carrierSustain = 255;
unsigned int carrierRelease = 768;

unsigned int FMRatio = 128; //Ratio relative to carrier where carrier = 256 
unsigned int FMAttack = 512; 
unsigned int FMDecay = 128;
unsigned int FMSustain = 128;
unsigned int FMRelease = 768;

//Global note properties
byte carrierEnvelopeStatus[4] = {0,0,0,0}; //Status of carrier envelope
unsigned int carrierEnvelopeValue[4] = {0,0,0,0}; //Value/position of envelope.
unsigned int carrierEnvelopeAttack = 0; //Attack value
unsigned int carrierEnvelopeDecay = 0; //Decay value
unsigned int carrierEnvelopeSustain = 0; //Sustain value
unsigned int carrierEnvelopeRelease = 0; //Release value
byte carrierAmplitude = 0; //General amplitude value
//Amount by which to increment index of sine table. 
unsigned int carrierNoteIncrement[4] = {0,0,0,0}; 
byte noteRange = loMid;

void loop() {
  //First, get input and interpret it. 

  prevInputStatus = inputStatus;
  inputStatus = PIND;
  byte pressedNote;
  byte releasedNote;
  if(inputStatus != prevInputStatus){
    if(inputStatus != idle){
      byte controlVal = (inputStatus & controlMask) >> 6;
      //Convert 2-bit octave and 4-bit note codes into single 0-47 range value. 
      byte noteVal = (inputStatus & noteMask) + (12 * ((inputStatus & octMask) >> 4));
      if(controlVal == noteOn){
        pressedNote = noteVal;
      }
      if(controlVal == noteOff){
        releasedNote = noteVal;
      }
    }
  }
  updatePulseWidth();//1st call
  updatePulseWidth();//2nd call

}
