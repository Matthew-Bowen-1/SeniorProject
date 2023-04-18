/*
  FM Synthesizer Core
  Written for Arduino Uno R3
  Matt Bowen 
  InlineBranch
*/

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

//Sound properties. Initialized within setup so that later they may be changed.
byte         loudness = 64; //Overall loudness
byte         noteRange = loMid; //Range of notes
//ADSR for carrier amplitude
unsigned int carrierAttack = 4096;
unsigned int carrierDecay = 512;
unsigned int carrierSustain = 220;
unsigned int carrierRelease = 128;

unsigned int FMLoudness = 3;
unsigned int FMRatio = 256+128;//Ratio relative to carrier where carrier = 256 
unsigned int FMAttack = 0x800; 
unsigned int FMDecay = 32;
unsigned int FMSustain = 8192;
unsigned int FMRelease = 82;
//Basic default setup. Hopefully makes good sound. 

//Used to set channel diagnostic bits.
byte chanDebugMask[4];
byte loopFlagMask;
byte oScopeTrigMask;
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
  TCCR1A = 0B10000010;
  TCCR1B = 0B00001001; //Same for all 3 modes
  //Only use Port D pins for input from controller
  DDRD = 0B00000000; //Set all pins of Port D to input
  PORTD = 0B11111111; //Set all pins high for pullup input. 
  inputStatus = PIND; //All musical input comes from PIND
  prevInputStatus = inputStatus; 
  //Initialize prevInputStatus so nothing happens on startup.

  //Arduino Pins D14-D17 go high when a channel is playing. Used for debugging.
  DDRC = DDRC | 0B00111111; //Set Port C pins 0-5 to output. 
  PORTC = PORTC & 0B11000000; //Initialize Port C pins 0-5 to low.
  for(int i = 0; i < 4; i++){
    chanDebugMask[i] = 1 << i;
  }
  loopFlagMask = 1 << 4;//Mask for loop flag.
  oScopeTrigMask = 1 << 5;//Mask for triggering oscilloscope. Not working.

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
  // while((TIFR1 & 0B00000001) != 1);
  // TIFR1 |= 0B00000001; 
  //Bit 0 of TIFR1 is the timer overflow bit.
  //For some reason it is cleared by writing a 1 to it. 
  //I may experiment with putting the waiting section after the calculations
  //but before the assignment of OCR1A
  
  //As it turns out the while loop can occur anywhere in this sequence.

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

  // if(carrierPhaseIdx[0] < 64){
  //   PORTC |= oScopeTrigMask;//Set high to indicate the low portion of a note
  // }else if (carrierPhaseIdx[0] >= 64){
  //   PORTC &=~ oScopeTrigMask;
  // } Currently not functional.

  while((TIFR1 & 0B00000001) != 1);
  TIFR1 |= 0B00000001;

  OCR1A = pulseWidth/128 + 256;
}

//Macros for input interpretation
#define idle 255
#define doNothing 0B00000011
#define noteOn 0B00000010
#define noteOff 0B00000001
#define config 0B00000000
byte controlMask = 0B11000000;
byte octMask = 0B00110000;
byte noteMask = 0B00001111;

//Macros for envelope status.
#define off 0
#define attack 1
#define decay 2
#define sustain 3
#define release 4

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

//Global note properties
byte notesPlaying[4] = {0,0,0,0};
//Which notes are being played
unsigned int noteDuration[4] = {0,0,0,0};
byte carrierEnvelopeStatus[4] = {0,0,0,0}; 
//State of carrier envelope. 
//0 = off, 1 = attack, 2 = decay, 3 = sustain, 4 = release
unsigned int carrierEnvelopeValue[4] = {0,0,0,0}; 
//Value/position of envelope
unsigned int carrierEnvelopeAttack = 0; 
//Attack value
unsigned int carrierEnvelopeDecay = 0; 
//Decay value
unsigned int carrierEnvelopeSustain = 0; 
//Sustain value
unsigned int carrierEnvelopeRelease = 0; 
//Release value
byte volume = 0; 
//digital volume value
unsigned int carrierNoteIncrement[4] = {0,0,0,0}; 
//Note's sine table increment
unsigned int FMNoteIncrement[4] = {0,0,0,0}; 
//FM sine table increment
byte FMEnvelopeStatus[4] = {0,0,0,0};
//State of FM envelope. Same values as with the carrier envelope.
unsigned int FMEnvelopeValue[4] = {0,0,0,0};
//Value of FM envelope for each channel
byte FMVolume = 0;
unsigned int FMEnvelopeAttack = 0; 
//FM attack value
unsigned int FMEnvelopeDecay = 0; 
//FM decay value
unsigned int FMEnvelopeSustain = 0; 
//FM sustain value
unsigned int FMEnvelopeRelease = 0; 
//FM release value


inline void getNoteStatus(byte *inputStatus, byte *pressedNote, byte *releasedNote) __attribute__((always_inline));
inline void getNoteStatus(byte *inputStatus, byte *pressedNote, byte *releasedNote){
  byte controlVal = (*inputStatus & controlMask) >> 6;

  //Convert 2-bit octave and 4-bit note codes into single 0-47 range value. 
  if(controlVal == noteOn){
    *pressedNote = (*inputStatus & noteMask) + (12 * ((*inputStatus & octMask) >> 4));
  }
  if(controlVal == noteOff){
    *releasedNote = (*inputStatus & noteMask) + (12 * ((*inputStatus & octMask) >> 4));
  }
  if(controlVal == config){
    //TODO: Implement config mode
  }
}

inline void assignChannel(byte *pressedNote, byte *channel) __attribute__((always_inline));
inline void assignChannel(byte *pressedNote, byte *channel){
  //Check if the pressed note is being played
  //This should only ever happen during the release portion of a note's ADSR
  if(carrierEnvelopeStatus[0] != off && *pressedNote == notesPlaying[0]) *channel = 0;
  if(carrierEnvelopeStatus[1] != off && *pressedNote == notesPlaying[1]) *channel = 1;
  if(carrierEnvelopeStatus[2] != off && *pressedNote == notesPlaying[2]) *channel = 2;
  if(carrierEnvelopeStatus[3] != off && *pressedNote == notesPlaying[3]) *channel = 3;

  //Use a channel if it is empty
  if(*channel == 255){
    if(carrierEnvelopeStatus[0] == off) *channel = 0;
    else if(carrierEnvelopeStatus[1] == off) *channel = 1;
    else if(carrierEnvelopeStatus[2] == off) *channel = 2;
    else if(carrierEnvelopeStatus[3] == off) *channel = 3;
  }

  //Determine the longest playing channel and use that
  if(*channel == 255){
    *channel = 0;
    for(byte i=1; i<4; i++){
      if(noteDuration[i] > noteDuration[*channel]) *channel = i;
    }
  }
}

inline void startNote(byte *channel, byte *pressedNote) __attribute__((always_inline));
inline void startNote(byte *channel, byte *pressedNote){
  carrierPhaseIdx[*channel] = 0;
  volume = loudness; //This is only to allow me to modify the volume 
  FMVolume = FMLoudness;
  if(noteRange == high){carrierNoteIncrement[*channel] = noteIncHigh[*pressedNote];}
  else if(noteRange == loMid){carrierNoteIncrement[*channel] = noteIncLoMid[*pressedNote];}
  else if(noteRange == hiMid){carrierNoteIncrement[*channel] = noteIncHiMid[*pressedNote];}
  else{carrierNoteIncrement[*channel] = noteIncBass[*pressedNote];}
  FMPhaseIdx[*channel] = 0;
  FMNoteIncrement[*channel] = ((long)carrierNoteIncrement[*channel] * FMRatio )/256;
  carrierEnvelopeAttack = carrierAttack;
  carrierEnvelopeDecay = carrierDecay;
  carrierEnvelopeSustain = carrierSustain << 8;
  carrierEnvelopeRelease = carrierRelease;
  carrierEnvelopeStatus[*channel] = attack;
  FMEnvelopeAttack = FMAttack;
  FMEnvelopeDecay = FMDecay;
  FMEnvelopeSustain = FMSustain;
  FMEnvelopeRelease = FMRelease;
  FMEnvelopeStatus[*channel] = attack;
  notesPlaying[*channel] = *pressedNote;
  noteDuration[*channel] = 0;
  PORTC |= (chanDebugMask[*channel]);//Set the corresponding debug pin high.
}

inline void stopNote(byte *releasedNote) __attribute__((always_inline));
inline void stopNote(byte *releasedNote){
  //Set ADSR status to release
  if(notesPlaying[0] == *releasedNote){
    carrierEnvelopeStatus[0] = 4; 
    FMEnvelopeStatus[0] = 4;
    PORTC &=~ chanDebugMask[0];//Set corresponding debug pin to low.
  }
  if(notesPlaying[1] == *releasedNote){
    carrierEnvelopeStatus[1] = 4; 
    FMEnvelopeStatus[1] = 4;
    PORTC &=~ chanDebugMask[1];
  }
  if(notesPlaying[2] == *releasedNote){
    carrierEnvelopeStatus[2] = 4; 
    FMEnvelopeStatus[2] = 4;
    PORTC &=~ chanDebugMask[2];
  }
  if(notesPlaying[3] == *releasedNote){
    carrierEnvelopeStatus[3] = 4; 
    FMEnvelopeStatus[3] = 4;
    PORTC &=~ chanDebugMask[3];
  }
  //If the received note isn't playing anyway then do nothing.
}

inline void calculateFMEnvelope() __attribute__((always_inline));
inline void calculateFMEnvelope(){
  for(byte chan = 0; chan < 4; chan++){
    switch(FMEnvelopeStatus[chan]){
      case attack:
        if((0xFFFF - FMEnvelopeValue[chan]) <= FMEnvelopeAttack){
          FMEnvelopeValue[chan] = 0xFFFF;
          FMEnvelopeStatus[chan] = decay;
        }
        else FMEnvelopeValue[chan] += FMEnvelopeAttack;
        break;
      case decay:
        if(FMEnvelopeValue[chan] <= (FMEnvelopeSustain + FMEnvelopeDecay)){
          FMEnvelopeValue[chan] = FMEnvelopeSustain;
          FMEnvelopeStatus[chan] = sustain;
        }
        else FMEnvelopeValue[chan] -= FMEnvelopeDecay;
        break;
      case sustain:
        break; //Nothing needed to be done
      case release:
        if(FMEnvelopeValue[chan] <= FMEnvelopeRelease){
          FMEnvelopeValue[chan] = 0;
          FMEnvelopeStatus[chan] = 0;
        }
        else FMEnvelopeValue[chan] -= FMEnvelopeRelease;
        break;
    }
  } 
}

inline void calculateCarrierEnvelope() __attribute__((always_inline));
inline void calculateCarrierEnvelope(){
  for (byte chan = 0; chan < 4; chan++){
    switch(carrierEnvelopeStatus[chan]){
      case attack:
        if((0xFFFF - carrierEnvelopeValue[chan]) <= carrierEnvelopeAttack){
          carrierEnvelopeValue[chan] = 0xFFFF;
          carrierEnvelopeStatus[chan] = decay;
        }
        else carrierEnvelopeValue[chan] += carrierEnvelopeAttack;
        break;
      case decay:
        if(carrierEnvelopeValue[chan] <= (carrierEnvelopeSustain + carrierEnvelopeDecay)){
          carrierEnvelopeValue[chan] = carrierEnvelopeSustain;
          carrierEnvelopeStatus[chan] = sustain;
        }
        else carrierEnvelopeValue[chan] -= carrierEnvelopeDecay;
        break;
      case sustain:
        break; //No modification necesary
      case release:
        if(carrierEnvelopeValue[chan] <= carrierEnvelopeRelease){
          carrierEnvelopeValue[chan] = 0;
          carrierEnvelopeStatus[chan] = 0;
        }
        else carrierEnvelopeValue[chan] -= carrierEnvelopeRelease;
        break;
    }

    noteDuration[chan]++;//do timing for note
    updatePulseWidth();//7th-10th calls
  }
}

inline void setEnvelopes() __attribute__((always_inline));
inline void setEnvelopes(){
  carrierAmplitude[0] = (volume * (carrierEnvelopeValue[0] >> 8)) >> 8;
  carrierPhaseInc[0] = carrierNoteIncrement[0];
  FMAmplitude[0] = (FMVolume * (FMEnvelopeValue[0] >> 8));
  FMPhaseInc[0] = FMNoteIncrement[0];
  updatePulseWidth();//11th call

  carrierAmplitude[1] = (volume * (carrierEnvelopeValue[1] >> 8)) >> 8;
  carrierPhaseInc[1] = carrierNoteIncrement[1];
  FMAmplitude[1] = (FMVolume * (FMEnvelopeValue[1] >> 8));
  FMPhaseInc[1] = FMNoteIncrement[1];
  updatePulseWidth();//12th call

  carrierAmplitude[2] = (volume * (carrierEnvelopeValue[2] >> 8)) >> 8;
  carrierPhaseInc[2] = carrierNoteIncrement[2];
  FMAmplitude[2] = (FMVolume * (FMEnvelopeValue[2] >> 8));
  FMPhaseInc[2] = FMNoteIncrement[2];
  updatePulseWidth();//13th call
  
  carrierAmplitude[3] = (volume * (carrierEnvelopeValue[3] >> 8)) >> 8;
  carrierPhaseInc[3] = carrierNoteIncrement[3];
  FMAmplitude[3] = (FMVolume * (FMEnvelopeValue[3] >> 8));
  FMPhaseInc[3] = FMNoteIncrement[3];
  updatePulseWidth();//14th call
}
/*
  The use of inline functions here is because I feel it is easier to understand
  when the code is layed out this way. This way I should be able to get the 
  runtime benefit as if I wrote all the code inline in the loop but with less 
  spaghetti mess.
*/
void loop() {
  //First, get input and interpret it. 
  PORTC |= loopFlagMask;//Set flag high
  prevInputStatus = inputStatus;
  inputStatus = PIND;
  PORTC &=~ loopFlagMask;//Set flag low
  byte pressedNote = 255;
  byte releasedNote = 255;
  
  if(inputStatus != prevInputStatus){
    if(inputStatus != idle){
      getNoteStatus(&inputStatus, &pressedNote, &releasedNote);
    }
  }
  updatePulseWidth();//1st call

  byte channel = 255; 

  updatePulseWidth();//2nd call

  assignChannel(&pressedNote, &channel);

  updatePulseWidth();//3rd call
  
  if(pressedNote != 255){
    startNote(&channel, &pressedNote);
  }

  updatePulseWidth();//4th call

  if(releasedNote != 255){
    stopNote(&releasedNote);
  }

  updatePulseWidth();//5th call

  calculateFMEnvelope();

  updatePulseWidth();//6th call

  calculateCarrierEnvelope();//7th-10th updatePulseWidth() calls are in here

  setEnvelopes();//11th-14th updatePulseWidth() calls are in here

  //Another update to note times
  noteDuration[0]++;
  noteDuration[1]++;
  noteDuration[2]++;
  noteDuration[3]++;

  updatePulseWidth();//15th call
}
