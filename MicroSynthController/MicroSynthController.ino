#include <MIDI.h>
MIDI_CREATE_DEFAULT_INSTANCE();
/*For non musicians, 'b' indicates a flat note
  C  0
  Db 1
  D  2
  Eb 3
  E  4
  F  5
  Gb 6
  G  7
  Ab 8
  A  9
  Bb 10
  B  11
*/

byte busVal = 0B11111111;

//Only 12 anotes in octave
byte noteVal = 15;
byte noteMask = 0B00001111;

//Synth core can only generate 48 notes, which is 4 octaves of 12 notes
byte octVal = 48;
byte octMask = 0B00110000;

//Use exact values to avoid bit shifts later
#define doNothing 0B11000000
#define noteOn 0B10000000
#define noteOff 0B01000000
#define config 0B00000000
byte control = doNothing;
byte controlMask = 0B11000000;
byte configVal = 0B00000001;
byte configValMask = 0B00000001;
bool configState = false;

byte getNoteVal(byte note){
  return note % 12;
}

//octMask = 00110000
byte getOctVal(byte note){
  byte tempOctVal = 0;

  if(note >= 48 && note <= 59){
    tempOctVal = 0B00000000;
  }
  if(note >= 60 && note <= 71){
    tempOctVal = 0B00010000;
  }
  if(note >= 72 && note <= 83){
    tempOctVal = 0B00100000;
  }
  if(note >= 84 && note <= 95){
    tempOctVal = 0B00110000;
  }
  return tempOctVal;
}

//The synth core cannot handle velocity. It is only used to determine note on/off
 
void HandleNoteOn(byte channel, byte note, byte velocity){ 
  //Set note and octave values based upon incoming midi signal
  //If note is outside range, do nothing.
  if(note >= 48 && note <= 95){
    octVal = getOctVal(note);
    noteVal = getNoteVal(note);
    control = noteOn;
  }
}

/*
  Uses note value to tell synth core which note to disable.
  This function only reacts to MIDI noteOff commands.
  This function tells the synth to turn off a note when it gets a MIDI
  noteOff command for that note.

*/
void HandleNoteOff(byte channel, byte note, byte velocity){
  noteVal = getNoteVal(note);
  octVal = getOctVal(note);
  control = noteOff;
}

void setup() {

  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(HandleNoteOn);
  MIDI.setHandleNoteOff(HandleNoteOff);
  //Set Port B pins to output
  /*
    On Arduino Micro (Atmega 32u4 chip), Port B pinout is as follows:
    PB7 - D11
    PB6 - D10
    PB5 - D9
    PB4 - D8
    PB3 - CIPO/D14 (Labeled "MI" on board)
    PB2 - D16/COPI (Labeled "IO" on board)
    PB1 - SCK/D15 (Labeled "SCK" on board)
    PB0 - D17/SS (Labeled "SS" on board)
    Designate all Port B pins as output
  */
  DDRB = 0B11111111;
  //Default all Port B pins to high
  PORTB = 0B11111111;

  //Pin A5 is bit 0 of Port F. It is the pin used for the config mode button.
  //Set bit 0 of DDRF to 0 to designate that pin as input
  DDRF &= 0B11111110;
  //Set pin high as it will be used in a pull-up configuration.
  PORTF |= 0B00000001;
}
byte prevBusVal;
byte repeatCount = 0;//Tracks how many times the current signal has been displayed.
byte prevConfigVal;

void loop() {
  //I am unsure whether to reset the bus or leave it be after each command

  prevBusVal = busVal;
  prevConfigVal = configVal;
  
  configVal = (PINF & configValMask);

  if(configVal != prevConfigVal){
    if((configVal == 0) && (configState == false)){
      configState = true;
      control = config;
      PORTB = (busVal & ~controlMask) | (control & controlMask);
    }
  }

  while(configState){
    prevConfigVal = configVal;
    configVal = (PINF & configValMask);
    
    if((configVal != prevConfigVal) && (configVal == 0)){
      configState = false;
      control = doNothing;
      PORTB = (busVal & ~controlMask) | (control & controlMask);
    }    
  }

  MIDI.read(); //Read for MIDI commands

  //set note value
  busVal = (busVal & ~noteMask) | (noteVal & noteMask);
  //Set octave value
  busVal = (busVal & ~octMask) | (octVal & octMask);
  //set numNotes value
  busVal = (busVal & ~controlMask) | (control & controlMask);



  //set all pins of PORTB simultaneously 

  PORTB = busVal;
}
