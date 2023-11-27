# SeniorProject
    This repository contains the code for a digital FM synthesizer running on
two Arduino microcontrollers. The synth can play up to 4 notes simultaneously,
and has a total range of 4 octaves at any given time which can be shifted up or
down by the user. The device features no keyboard, instead being controlled via
a MIDI input as well as seven push buttons.
 
    The two Arduino microcontrollers split the duty of receiving MIDI commands
and generating sound. The first controller, an Arduino Micro, receives the MIDI
commands and translates them into a unique 8-bit signature that is then written
to an 8-bit bus that connects the Arduino Micro to the Arduino Uno. In order to
avoid unwanted sound artifacts, the Arduino Uno runs in an infinite loop with
interrupts disabled. This means that it is unable to receive serial data such
as MIDI, requiring the 8-bit bus instead. Once every loop, the Uno reads this
bus and acts accordingly. The bus can tell it to play a new note, stop a note
from playing, or enter a 'config mode'.

    The config mode is selected by the user pressing the config pushbutton,
connected to the Arduino Micro. When pressed, a special command is sent via the
8-bit bus to the Arduino Uno, telling it to re-enable interrupts and establish
an I2C connection with the Arduino Micro. While this connection is established,
the user can use 6 push buttons connected to the Micro to modify several sound
generation parameters stored in the Uno. Two buttons add or subtract a given
increment size to a parameter, two buttons change the increment size, and two
buttons cycle through the parameters.

    All the code for this project was written in the Arduino IDE. I was rather
unfamiliar with Arduino when I originally worked on this project, so I figured
that using the Arduino IDE would be a good place to start. However, the Arduino
IDE is rather barebones. I purposefully chose not to split my code into
libraries as the Arduino IDE makes it somewhat difficult to track a project
with multiple files. 

    This project is a bit messy. As a result of using the Arduino IDE, too much
code is contained in each file. Additionally, this project relies far too much
on global variables. While this is a simple embedded project, it remains my
largest criticism that it is so disprganized. 
