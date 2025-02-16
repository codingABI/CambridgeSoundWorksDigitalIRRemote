/*
 * Project: CambridgeSoundWorksDigitalIRRemote (Ding25)
 * Description:
 * A Digispark/ATtiny85 DIY based IR (infra-red) remote control for a vintage 
 * Cambridge SoundWorks Digital CSW250 speaker system. 
 * 
 * The CambridgeSoundWorksDigitalIRRemote replaces the original speaker system "remote" control, 
 * which has some disadvantages:
 * - Cable bounded (Max. 2 meters distance)
 * - Without turning off the speaker system, the speaker system has an idle power consumption of about 7.5 W (even without audio input)
 * - With turning off the speaker system, the system has a power consumption of about 3.1 W
 * - To turn off the speaker system, the volume level has to be set to minimum volume level
 * - After turning on the speaker system the previous volume level has to restored manually
 * 
 * The CambridgeSoundWorksDigitalIRRemote allows me use the IR wireless controller from my TV to
 * - turn on/off the speaker system
 * - automatically restore the last used volume level after turning on
 * - change the volume level
 * - mute/unmute
 * 
 * License: CC0
 * Copyright (c) 2025 codingABI
 * For details see: License.txt
 *
 * created by codingABI https://github.com/codingABI/CambridgeSoundWorksDigitalIRRemote
 * 
 * Used external board and libraries from Arduino IDE Board and Library Manager:
 * - Board: ATTinyCore 1.5.2, LGPL version 2.1 by SpenceKonde 
 * - Libraries: 
 *   - IRremote (4.4.1, MIT license)
 *     - Initially coded 2009 Ken Shirriff http://www.righto.com
 *     - Copyright (c) 2016-2017 Rafi Khan https://rafikhan.io
 *     - Copyright (c) 2020-2024 Armin Joachimsmeyer
 *
 * Hardware:
 * - Digispark attiny85 Microcontroller
 *   Reset pin configured as I/O pin
 *   Boardmanager:
 *      Board: ATtiny85 (Micronucleus / Digispark)
 *      Clock: "8 MHz (no USB)" 
 * - Digital potentiometer X9C103
 * - IR-Receiver VS1838B
 * - Optocoupler 817A
 * - Cambridge SoundWorks Digital CSW250
 * - Sony RM-ED011 remote control
 * 
 * This project was designed only for my combination: Cambridge SoundWorks Digital CSW250 and Sony RM-ED011
 * Other remote controls could also work, if you find the appropriate IR signals and adjust this code
 *
 * History:
 * 20250213, Initial version
 *
 */

// My IR remote control is a Sony RM-ED011
#define DECODE_SONY
#include <IRremote.hpp> 

#include <EEPROM.h>
#include <avr/sleep.h>

// Pin definitions
#define AMPLIFIER_PIN 5 // Pin to turn on/off the amplifier with an optocoupler
#define INC_PIN 2 // INC pin for the digital potentiometer
#define UD_PIN 1 // UP/DOWN pin for the digital potentiometer
#define IRRX_PIN  0 // IR receiver pin

// Max volume levels minus one for digital potentiometer x9c103 
#define MAXVOLUME 99
// Accept IR signals only after IRDEADTIMEMS has expired to avoid bouncing and too frequent signals
#define IRDEADTIMEMS 200
// Startaddress in EEPROM for permanent settings
#define EEPROMADDR_START 0
// First byte in EEPROM at begin of startaddress (=Signature)
#define EEPROM_SIGNATURE 25
// Second byte in EEPROM (=Version)
#define EEPROM_VERSION 1
// Length for EEPROM_SIGNATURE and EEPROM_VERSION
#define EEPROM_HEADERSIZE 2

bool g_mute = false; // True, if volume is muted
byte g_volume = 0; // Current volume level
bool g_amplifierEnabled = false; // True, if amplifier is turned on

// Get stored volume level from EEPROM
byte getVolumeFromEEPROM() {
  byte result = MAXVOLUME/2; // Default volume level to 50%
  int addr = EEPROMADDR_START;

  if ( (EEPROM.read(addr) == EEPROM_SIGNATURE) && (EEPROM.read(addr+1) == EEPROM_VERSION) ) { // Check signature & version
    addr+=EEPROM_HEADERSIZE;
    result = EEPROM.read(addr);
    if (result > MAXVOLUME) result = MAXVOLUME;
  }
  return result;
}

// Store volume level to EEPROM
void setVolumeToEEPROM(byte value) {
  int addr = EEPROMADDR_START;
  EEPROM.update(addr,EEPROM_SIGNATURE);
  addr+=sizeof(byte);
  EEPROM.update(addr,EEPROM_VERSION);
  addr+=sizeof(byte);
  EEPROM.update(addr,value);
}

// Change wiper position of digital potentiometer (Up or down depends on UD_PIN)
void wiperSteps(byte steps) {
  for (byte i=0;i<steps;i++) {
    digitalWrite(INC_PIN,LOW);
    delayMicroseconds(2); // Datasheet for x9c103 says: at least 1us
    digitalWrite(INC_PIN,HIGH);
    delayMicroseconds(2); // Datasheet for x9c103 says: at least 1us
  }
}

// Increate volume level
void volumeStepUp(byte steps = 1) {
  digitalWrite(UD_PIN,HIGH); // UP
  delayMicroseconds(4); // // Datasheet for x9c103 says: at least 2.9us
  wiperSteps(steps);
  digitalWrite(UD_PIN,LOW); // Set up/down pin to low to disable the builtin LED 
  delayMicroseconds(4); // // Datasheet for x9c103 says: at least 2.9us
  if (g_volume <= MAXVOLUME-steps) g_volume+=steps; else g_volume = MAXVOLUME;
}

// Decrease volume level
void volumeStepDown(byte steps = 1)
{
  digitalWrite(UD_PIN,LOW); // Down
  delayMicroseconds(4); // Datasheet for x9c103 says: at least 2.9us
  wiperSteps(steps);
  if (g_volume >= steps) g_volume-=steps; else g_volume = 0;
}

// Mute the volume
void volumeMute() {
  // Move wiper to 0
  volumeStepDown(MAXVOLUME);
  g_volume = 0;
  g_mute = true;
}

// Restore volume level
void volumeRestore() {
  if (!g_mute) volumeMute(); // If not alreay muted => mute
  // Move wiper to stored value
  volumeStepUp(getVolumeFromEEPROM());
  g_mute = false;
}

// Turn on Cambridge SoundWorks amplifier
void applifierTurnOn(){
  volumeRestore(); // Restore last stored volume level
  pinMode(AMPLIFIER_PIN,OUTPUT);
  digitalWrite(AMPLIFIER_PIN,HIGH);
  g_amplifierEnabled = true;
}

// Turn off Cambridge SoundWorks amplifier
void amplifierTurnOff(){
  volumeMute(); // Mute to prevent noise when amplifier is turned off
  digitalWrite(AMPLIFIER_PIN,LOW);
  pinMode(AMPLIFIER_PIN,INPUT);
  g_amplifierEnabled = false;
}

void setup() 
{ 
  // Pin modes
  pinMode(INC_PIN,OUTPUT);
  pinMode(UD_PIN,OUTPUT);

  // IR init
  IrReceiver.begin(IRRX_PIN);

  // Init digital potentiometer 
  digitalWrite(INC_PIN,HIGH);
  
  // Turn on Cambridge SoundWorks amplifier
  applifierTurnOn();
} 

void loop() {
  static unsigned long lastIRSignalMS = 0;

  if (IrReceiver.decode()) {
    IrReceiver.resume(); // Enable receiving of the next value

    if (millis()-lastIRSignalMS > IRDEADTIMEMS) { // Debouce
      switch ((IrReceiver.decodedIRData.address << 8) + IrReceiver.decodedIRData.command) {
        case 0x971C: // >> Volume up
          if (!g_amplifierEnabled) break;
          if (g_mute) volumeRestore();
          volumeStepUp();
          setVolumeToEEPROM(g_volume);
          g_mute=false;
          break;
        case 0x971B: // << Volume down
          if (!g_amplifierEnabled) break;
          if (g_mute) volumeRestore();
          volumeStepDown();
          setVolumeToEEPROM(g_volume);
          g_mute=false;
          break;
        case 0x9719: // || Mute/unmute
          if (!g_amplifierEnabled) break;
          if (g_mute) volumeRestore(); else volumeMute();
          break;
        // TV can be switched on by various buttons
        case 0x0100: // 1
        case 0x0101: // 2
        case 0x0102: // 3
        case 0x0103: // 4
        case 0x0104: // 5
        case 0x0105: // 6
        case 0x0106: // 7
        case 0x0107: // 8
        case 0x0108: // 9
        case 0x0109: // 0
        case 0x7752: // Digital
        case 0x0139: // Analog
        case 0x0110: // Channel +
        case 0x0111: // Channel -
        case 0x0112: // Volume +
        case 0x0113: // Volume -
        case 0x0114: // Mute/unmute
          if (!g_amplifierEnabled) applifierTurnOn();
          break;
        case 0x0115: // I/O Turn on/off amplifier
          if (g_amplifierEnabled) amplifierTurnOff(); else applifierTurnOn();
          break;
      }
      lastIRSignalMS = millis();
    }
  }  

  // Enter IDLE sleep to save power
  set_sleep_mode(SLEEP_MODE_IDLE);
  sleep_mode();
}