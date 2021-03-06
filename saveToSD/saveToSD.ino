/* A basic datalogger script from the Cave Pearl Project that sleeps the datalogger and wakes from DS3231 RTC alarms*/

// This code is part of the online tutorials supporting for the Sensors publication at: https://www.mdpi.com/1424-8220/18/2/530
// the latest 'EDU' iteration of that design is https://thecavepearlproject.org/2019/02/21/easy-1-hour-pro-mini-classroom-datalogger-build-update-feb-2019/
// but it will run on any of the Pro Mini dataloggers described at https://thecavepearlproject.org/how-to-build-an-arduino-data-logger/
// If you use this material as part of another academic project, be cool and reference the paper when you publish. Cheers!

//updated 20190118 with support for unregulated systems running directly from 2xAA lithium batteries
//updated 20190204 with dynamic preSDsaveBatterycheck safety check
//updated 20190219 with support for using indicator LED as a light sensor
//updated 20190720 with support for DS18b20 Temperature sensor, set to 12-bit
//updated 20190720 with support for tweaking the internal vref constant for accurate rail voltages
//updated 20190722 with 'delayed start' in setup
//updated 20210217 with improvements to LED light sensing

#include <Wire.h>
#include <SPI.h>
#include <avr/power.h>
#include <RTClib.h>     // https://github.com/MrAlvin/RTClib         // Note: there are many other DS3231 libs availiable
#include <LowPower.h>   // https://github.com/rocketscream/Low-Power //for low power sleeping between readings
#include <SdFat.h>      // https://github.com/greiman/SdFat          //needs 512 byte ram buffer!
#include <math.h>       /* atan */

//============ CONFIGURATION SETTINGS =============================
//change the text inside the brackets here to suit your configuration
const char deploymentDetails[] PROGMEM = "Reeferees Logger #1,magnetometer and accelerometer used as sensor,1134880L constant,UTC time set,if found contact: yourname@email.edu"; 
const char dataCollumnLabels[] PROGMEM = "TimeStamp,Battery(mV),SDsaveDelta(mV),Compass Reading (deg),Tilt Angle, Velocity (m/s), Raw Accel_X Reading (m/s^2),Raw Accel_Y Reading (m/s^2),Raw Accel_Z Reading (m/s^2), Raw Mag_X Reading (microT), Raw Mag_Y Reading (microT), Raw Mag_Z Reading (microT)"; //column header labels for your data
//more info on the PROGMEM modifier @ http://www.gammon.com.au/progmem

#define SampleIntervalMinutes 1  // Options: 1,2,3,4,5,6,10,12,15,20,30 ONLY (must be a divisor of 60)
                                 // number of minutes the loggers sleeps between each sensor reading

//#define ECHO_TO_SERIAL // this enables debugging output to the serial monitor when your logger is powered via USB/UART
                         // comment out this define when you are deploying your logger in the field 
                         // enabling ECHO_TO_SERIAL also skips the alarm sync delay at the end of startup funtion

//uncomment ONLY ONE of following -> depending on how you are powering your logger
//#define voltageRegulated  // if you connect the battery supply through the Raw & GND pins & use the ProMini's regulator
#define unregulated2xLithiumAA  // define this if you've removed the regulator and are running directly from 2xAA lithium batteries

#define InternalReferenceConstant 1126400L  //used for reading the rail voltage reading
// The "default" value of 1126400L, This assumes the internal vref is perfect 1.1v (i.e. 1100mV times 1024 ADC levels is 1126400)
// but in reality the internal ref. varies by ±10% - to make the Rail/Battery readings more accurate use the CalVref utility from OpenEnergyMonitor
// https://github.com/openenergymonitor/emontx2/blob/master/firmware/CalVref/CalVref.ino to get the constant for your particular Arduino

// This code assumes you have a common cathode RGB led as the indicator on your logger
// you can read only one LED color channel as a light sensor by disabling the other two defines here
// At low light levels it can take several seconds for each channel to make a reading
// So use a sampling interval longer than 1 minute if you read all three colors or you could over-run the wakeup alarm (which causes a 24hour sleep interval)
//#define readRedLED ON // enabling readLEDsensor define ADDS LED AS A SENSOR readings to the loggers default operation
//#define readGreenLED ON // enabling readLEDsensor define ADDS LED AS A SENSOR readings to the loggers default operation 
//#define readBlueLED ON // enabling readLEDsensor define ADDS LED AS A SENSOR readings to the loggers default operation 
#define LED_GROUND_PIN 4 // to use the indicator LED as a light sensor you must ground it through a digital I/O pin from D3 to D7
#define RED_PIN 3   //change these numbers to suit the way you connected the indicator LED
#define GREEN_PIN 5
#define BLUE_PIN 6 
// Note: I always turn on indicator LEDs via INPUT_PULLUP, rather than simply setting the pin to OUTPUT & HIGH,
// this saves power & adds short circuit safety in case the LED was connected without limit resistor - but the light is dim

SdFat sd; /*Create the objects to talk to the SD card*/
SdFile file;
const int chipSelect = 10;    //CableSelect moved to pin 10 in this build
char FileName[12] = "data000.csv"; //note: this gets updated to a new number if the file aready exists on the SD card
const char codebuild[] PROGMEM = __FILE__;  // loads the compiled source code directory & filename into a varaible
const char compileDate[] PROGMEM = __DATE__; 
const char compileTime[] PROGMEM = __TIME__;

// variables for reading the RTC time & handling the D2=INT(0) alarm interrupt signal it generates
RTC_DS3231 RTC; // creates an RTC object in the code
#define DS3231_I2C_ADDRESS 0x68
#define DS3231_CONTROL_REG 0x0E
#define RTC_INTERRUPT_PIN 2
byte Alarmhour;
byte Alarmminute;
byte Alarmday;
char TimeStamp[ ] = "0000/00/00,00:00"; //16 ascii characters (without seconds because they are always zeros on wakeup)
volatile boolean clockInterrupt = false;  //this flag is set to true when the RTC interrupt handler is executed
float rtc_TEMP_degC;

int BatteryReading = 9999; //often read from a 10M/3.3M voltage divider, but could aso be same as VccBGap when unregulated
int safetyMargin4SDsave = 100; // grows dynamically after SD save events - tends to get larger as batteries age
int systemShutdownVoltage = 2850; // updated later depending on how you power your logger
//if running from 2x AA cells (with no regulator) the input cutoff voltage should be ~2850 mV (or higher)
//if running a unit with the voltage regulator the absolute minumum input cutoff voltage is 3400 mV (or higher)
#ifdef voltageRegulated
#define BatteryPin A0  //only used if you have a voltage divider to put input on A0 - change to suit your actual connection
#endif

//Global variables :most are used for temporary storage during communications & calculations
//===========================================================================================
//bool bitBuffer;         // for fuctions that return an on/off true/false state
byte bytebuffer1 = 0;     // for functions that return a byte - usually comms with sensors
byte bytebuffer2 = 0;     // second buffer for 16-bit sensor register readings
int integerBuffer = 9999;    // for temp-swapping ADC readings
float floatbuffer = 9999.9;  // for temporary float calculations
#define analogInputPin A0    //for analog pin reading
int analogPinReading = 0;

//Sensor specific variables & defines:
//====================================
//enable TS_DS18B20 only if installed - otherwise comment out if not connected:
//#define TS_DS18B20 8    //set this to the INPUT PIN connected to the sensors DATA wire
// & don't forget you need a 4K7 pullup resistor (joining that data line to the high rail) for the DS18b20 to operate properly
//#if defined(TS_DS18B20)   // variables for DS18B20 temperature sensor only included if #define TS_DS18B20
//#include <OneWire.h>      // this sensor library from  http://www.pjrc.com/teensy/td_libs_OneWire.html
//OneWire ds(TS_DS18B20);      
// byte addr[8];
// int ds18b20_TEMP_Raw = 0;
// float ds18b20_TEMP_degC= 0.0;
//#endif  //defined(TS_DS18B20)

#include <Adafruit_Sensor.h>
//#include <Adafruit_LSM303DLH_Mag.h>
#include <Adafruit_LSM303_Accel.h>

// CHANGE COMPASS
#include <LSM303.h>
LSM303 compass;

/* Assign a unique ID to this sensor at the same time */
Adafruit_LSM303_Accel_Unified accel = Adafruit_LSM303_Accel_Unified(54321);
//Adafruit_LSM303DLH_Mag_Unified mag = Adafruit_LSM303DLH_Mag_Unified(12345); // CHANGE COMPASS
//======================================================================================================================
//  *  *   *   *   *   *   SETUP   *   *   *   *   *
//======================================================================================================================

void setup() {

// builds that jumper A4->A2 and A5->A3 to bring the I2C bus to the screw terminals MUST DISABLE digital I/O on these two pins
// If you are doing only ADC conversions on the analog inputs you can disable the digital buffers on those pins, to save power
bitSet (DIDR0, ADC0D);  // disable digital buffer on A0
bitSet (DIDR0, ADC1D);  // disable digital buffer on A1
bitSet (DIDR0, ADC2D);  // disable digital buffer on A2
bitSet (DIDR0, ADC3D);  // disable digital buffer on A3
//Once the input buffer is disabled, a digitalRead on those A-pins will always be zero.

  #ifdef voltageRegulated
  systemShutdownVoltage = 3400; // 3400 is the minimum allowd input to the Mic5205 regulator - alkalines often drop by 200mv or more under load
  #endif
  
  #if defined (unregulated2xLithiumAA) || defined(ECHO_TO_SERIAL) // two situations with no voltage on the A6 resistor divider
  systemShutdownVoltage = 2800; // minimum Battery voltage when running from 2x LITHIUM AA's
  #endif
  
  // Setting the SPI pins high helps some sd cards go to sleep faster
  pinMode(chipSelect, OUTPUT); digitalWrite(chipSelect, HIGH); //ALWAYS pullup the ChipSelect pin with the SD library
  //and you may need to pullup MOSI/MISO, usually MOSIpin=11, and MISOpin=12 if you do not already have hardware pulls
  pinMode(11, OUTPUT);digitalWrite(11, HIGH); //pullup the MOSI pin on the SD card module
  pinMode(12, INPUT_PULLUP); //pullup the MISO pin on the SD card module
  // NOTE: In Mode (0), the SPI interface holds the CLK line low when the bus is inactive, so DO NOT put a pullup on it.
  // NOTE: when the SPI interface is active, digitalWrite() cannot affect MISO,MOSI,CS or CLK

  // 24 second time delay - stabilizes system after power connection
  // the 104 cap on the main battery voltage divider needs > 2s to charge up (on regulated systems)
  // delay also prevents writing multiple file headers with brief accidental power connections
  digitalWrite(BLUE_PIN, LOW); 
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(RED_PIN, LOW); 
  #ifdef LED_GROUND_PIN
  digitalWrite(LED_GROUND_PIN, LOW);  //another pin to sink current - depending on the wireing
  pinMode(LED_GROUND_PIN, OUTPUT);   //units using pre-made LED boards sometimes need to set
  #endif 
  pinMode(RED_PIN,INPUT_PULLUP);     // Using INPUT_PULLUP instead of HIGH lets you connect a raw LED safely
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_ON);
  digitalWrite(RED_PIN, LOW);
  pinMode(BLUE_PIN, INPUT_PULLUP);
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_ON);
  digitalWrite(BLUE_PIN, LOW);
  pinMode(GREEN_PIN, INPUT_PULLUP); //green led is usually 4x as bright as the others
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_ON);
  digitalWrite(GREEN_PIN, LOW); 
  pinMode(RED_PIN,INPUT_PULLUP);    // red is usually dimmest color
  
  Serial.begin(115200);    // Always serial.begin because if 'anything' in some random library tries to print without it you get a HARD system freeze
  Wire.begin();          // Start the i2c interface

  // CHANGE COMPASS
  compass.init();
  compass.enableDefault();
  
  RTC.begin();           // RTC initialization:
  RTC.turnOffAlarm(1);
  clearClockTrigger();   // Function stops RTC from holding interrupt line low after power reset
  pinMode(RTC_INTERRUPT_PIN,INPUT_PULLUP);  //not needed if you have hardware pullups on SQW, most RTC modules do but some don't
  DateTime now = RTC.now();
  sprintf(TimeStamp, "%04d/%02d/%02d %02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute());
  enableRTCAlarmsonBackupBattery(); // only needed if you cut the pin supplying power to the DS3231 chip

  #if defined (unregulated2xLithiumAA) || defined(ECHO_TO_SERIAL) 
  BatteryReading=getRailVoltage(); //If you are running from raw battery power (with no regulator) VccBGap IS the battery voltage
  #else  // #ifdef voltageRegulated:
  analogReference(DEFAULT);analogRead(BatteryPin); delay(10);  //throw away the first reading when using high impedance voltage dividers!
  floatbuffer = float(analogRead(BatteryPin));
  floatbuffer = (floatbuffer+0.5)*(3.3/1024.0)*4.030303; // 4.0303 = (Rhigh+Rlow)/Rlow for a 10M/3.3M voltage divider combination
  BatteryReading=int(floatbuffer*1000.0);
  #endif
  
  if (BatteryReading < (systemShutdownVoltage+safetyMargin4SDsave+50)) { 
    error_shutdown(); //if the battery voltage is too low to create a log file, shut down the system
  }  //a 50mv dip on the battery supply is quite normal after 100mA SDwrite loads, but can go up to 200-300mv as batteries age

#ifdef ECHO_TO_SERIAL
  Serial.println(F("Initializing SD card..."));
#endif
  // see if the card is present and can be initialized
  if (!sd.begin(chipSelect,SPI_FULL_SPEED)) {   // some cards may need SPI_HALF_SPEED
    #ifdef ECHO_TO_SERIAL
    Serial.println(F("Card failed, or not present"));
    Serial.flush();
    #endif
    error_shutdown(); //if you cant initialise the SD card, you can't save any data - so shut down the logger
    return;
  }
  #ifdef ECHO_TO_SERIAL
  Serial.println(F("card initialized."));
  #endif
  delay(25); //sd.begin hits the power supply pretty hard
  
// Find the next availiable file name // from https://learn.adafruit.com/adafruit-feather-32u4-adalogger/using-the-sd-card
// generates a new file on every restart. If you SD card is mysteriously filling up with files then there's a good
// chance you logger is brown out power cycling -> check if one of your sensors is pulling too much current.
// O_CREAT = create the file if it does not exist,  O_EXCL = fail if the file exists, O_WRITE - open for write
  if (!file.open(FileName, O_CREAT | O_EXCL | O_WRITE)) { // note that every system restart will generate new log files!
    for (int i = 1; i < 500; i++) {  // FAT16 has a limit of 512 files entries in root directory
      delay(5);
      snprintf(FileName, sizeof(FileName), "data%03d.csv", i);//concatenates the next number into the filename
      if (file.open(FileName, O_CREAT | O_EXCL | O_WRITE)) 
      {
        break; //if you can open a file with the new name, break out of the loop
      }
    }
  }
  delay(25);
  //write the header information in the new file
  file.print(F("Filename:"));
  file.println((__FlashStringHelper*)codebuild); // writes the entire path + filename to the start of the data file
  file.print(F("Uploaded to this logger:,,"));
  file.print((__FlashStringHelper*)compileDate);
  file.print(F(",,,at:"));
  file.print((__FlashStringHelper*)compileTime);
  file.println();file.println((__FlashStringHelper*)deploymentDetails);
  file.println();file.println((__FlashStringHelper*)dataCollumnLabels);
  file.close();//Note: SD cards can continue drawing system power for up to 1 second after file close command
  digitalWrite(RED_PIN, LOW);
  
#ifdef ECHO_TO_SERIAL
  Serial.print(F("Data Filename:")); Serial.println(FileName); Serial.println(); Serial.flush();
#endif

//====================================================================
//MAG + ACCEL initialization 
//====================================================================
#ifndef ESP8266
  while (!Serial)
    ; // will pause Zero, Leonardo, etc until serial console opens
#endif
  Serial.begin(115200);
  Serial.println("Accelerometer Test");
  Serial.println("");

  /* Initialise the sensor */
  if (!accel.begin()) {
    /* There was a problem detecting the ADXL345 ... check your connections */
    Serial.println("Ooops, no LSM303 detected ... Check your wiring!");
    while (1)
      ;
  }
  /* Display some basic information on this sensor */
//  displayAccSensorDetails();

  // --------
  // CHANGE COMPASS
//
//  Serial.println("Magnetometer Test");
//  Serial.println("");
// 
//  /* Enable auto-gain */
//  mag.enableAutoRange(true);
// 
//  /* Initialise the sensor */
//  if (!mag.begin()) {
//    /* There was a problem detecting the LSM303 ... check your connections */
//    Serial.println("Ooops, no LSM303 detected ... Check your wiring!");
//    while (1)
//      ;
//  }
  /* Display some basic information on this sensor */
//  displayMagSensorDetails();
 
//#ifdef TS_DS18B20
//  
//  ds.search(addr);
//  
//  if ( !ds.search(addr))
//  {
//    Serial.println(F("ERROR: Did not find the DS18B20 Temp Sensor!"));Serial.flush();
//    return;
//  }
//  else
//  { 
//    //set the DS18b20 to 12 bit (high resolution) mode
//    ds.reset();             // rest 1-Wire
//    ds.select(addr);        // select DS18B20
//    ds.write(0x4E);         // write on scratchPad
//    ds.write(0x00);         // User byte 0 - Unused
//    ds.write(0x00);         // User byte 1 - Unused
//    ds.write(0x7F);         // set up en 12 bits (0x7F)
//    ds.reset();             // reset 1-Wire
//    ds.select(addr);        // select DS18B20 
//    ds.write(0x48);         // copy scratchpad to EEPROM
//    delay(15);              // wait for end of EEPROM write
//    
//    Serial.print(F("DS18B20 found @ ROM addr:"));
//    for (uint8_t i = 0; i < 8; i++) {
//      Serial.write(' ');
//      Serial.print(addr[i], HEX);
//    }
//    Serial.println();Serial.flush();
//  }  // if ( !ds.search(addr))
//  
//#endif  //for #ifdef TS_DS18B20
//
////setting UNUSED digital pins to INPUT_PULLUP reduces noise & risk of accidental short
////pinMode(7,INPUT_PULLUP); //only if you do not have anything connected to this pin
////pinMode(8,INPUT_PULLUP); //only if you do not have anything connected to this pin
////pinMode(9,INPUT_PULLUP); // NOT if you are you using this pin for the DS18b20!
//#ifndef ECHO_TO_SERIAL
// pinMode(0,INPUT_PULLUP); //but not if we are connected to usb
// pinMode(1,INPUT_PULLUP); //then these pins are needed for RX & TX 
//#endif

//==================================================================================================================
//Delay logger start until alarm times are in sync with sampling intervals
//this delay prevents a "short interval" from occuring @ the first hour rollover

#ifdef ECHO_TO_SERIAL  
  Serial.println(F("Timesync startup delay is disabled when ECHO_TO_SERIAL enabled"));
  Serial.flush();
#else   //sleep logger till alarm time is sync'd with sampling intervals
  Alarmhour = now.hour(); Alarmminute = now.minute();
  int syncdelay=Alarmminute % SampleIntervalMinutes;  // 7 % 10 = 7 because 7 / 10 < 1, e.g. 10 does not fit even once in seven. So the entire value of 7 becomes the remainder.
  syncdelay=SampleIntervalMinutes-syncdelay; // when SampleIntervalMinutes is 1, syncdelay is 1, other cases are variable
  Alarmminute = Alarmminute + syncdelay;
  if (Alarmminute > 59) {  // check for roll-overs
  Alarmminute = 0; Alarmhour = Alarmhour + 1; 
  if (Alarmhour > 23) { Alarmhour = 0;}
  }
  RTC.setAlarm1Simple(Alarmhour, Alarmminute);
  RTC.turnOnAlarm(1); //purple indciates logger is in delay-till-startup state
  pinMode(RED_PIN,INPUT_PULLUP);pinMode(BLUE_PIN,INPUT_PULLUP);// red&blue combination is ONLY used for this
  attachInterrupt(0, rtcISR, LOW);  // program hardware interrupt to respond to D2 pin being brought 'low' by RTC alarm
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_ON);  //this puts logger to sleep
  detachInterrupt(0); // disables the interrupt after the alarm wakes the logger
  RTC.turnOffAlarm(1); // turns off the alarm on the RTC chip
#endif  //#ifdef ECHO_TO_SERIAL 

 digitalWrite(RED_PIN, LOW);digitalWrite(GREEN_PIN, LOW);digitalWrite(BLUE_PIN, LOW);//turn off all indicators
//====================================================================================================
}   //   terminator for setup
//=====================================================================================================

// ========================================================================================================
//      *  *  *  *  *  *  MAIN LOOP   *  *  *  *  *  *
//========================================================================================================

void loop() {
  pinMode(BLUE_PIN,INPUT_PULLUP); 
  DateTime now = RTC.now(); // reads time from the RTC
  sprintf(TimeStamp, "%04d/%02d/%02d %02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute());
  //loads the time into a string variable - don’t record seconds in the time stamp because the interrupt to time reading interval is <1s, so seconds are always ’00’  

 #ifdef ECHO_TO_SERIAL
   Serial.print("System taking a new reading at:");  //(optional) debugging message
   Serial.println(TimeStamp);Serial.flush();
 #endif

//// read the RTC temp register - Note: the DS3231 temp registers only update every 64seconds
//  Wire.beginTransmission(DS3231_I2C_ADDRESS);
//  Wire.write(0x11);       //the register where the temp data is stored
//  Wire.endTransmission(); // nothing really happens until the complier sends the .endTransmission command
//  Wire.requestFrom(DS3231_I2C_ADDRESS, 2);   //ask for two bytes of data
//  if (Wire.available()) {
//  byte tMSB = Wire.read();            //2’s complement int portion
//  byte tLSB = Wire.read();             //fraction portion
//  rtc_TEMP_degC = ((((short)tMSB << 8) | (short)tLSB) >> 6) / 4.0;  // Allows for readings below freezing: thanks to Coding Badly
//  rtc_TEMP_degC = (rtc_TEMP_degC * 1.8) + 32.0; // To Convert Celcius to Fahrenheit
//}
//else {
//  rtc_TEMP_degC = 999.9;  //if rtc_TEMP_degC contains 999.9, then you had a problem reading from the RTC!
//}
//#ifdef ECHO_TO_SERIAL
//Serial.print(F(" TEMPERATURE from RTC is: "));
//Serial.print(rtc_TEMP_degC); 
//Serial.println(F(" Celsius"));
//Serial.flush();
//#endif
  
digitalWrite(BLUE_PIN, LOW); //end of RTC communications
pinMode(GREEN_PIN,INPUT_PULLUP); //green indicates sensor readings taking place
LowPower.powerDown(SLEEP_30MS, ADC_OFF, BOD_ON); //optional delay here to make indicator pip more visible
//============================================================
// Read Analog Input
analogReference(DEFAULT);analogRead(analogInputPin); //always throw away the first ADC reading
delay(10);  //10msec delay gives Aref capacitor time to adjust

//now you can do a single analog reading one time 
analogPinReading = analogRead(analogInputPin);
// OR you can read the analog input line multiple times, and feed those readings into an averaging or smoothing filter
// One of my favorites for removing "single spike" errors from noisy sensor inputs is median3 which takes three values/readings as input
  
//    analogPinReading = median_of_3( analogRead(analogInputPin), analogRead(analogInputPin), analogRead(analogInputPin));
  
//you can use this filter with any sensor that generates only positive integer values

//=====================================
//Read the DS18b20 temperature Sensor:
//#ifdef TS_DS18B20    
//  ds18b20_TEMP_Raw = readDS18B20Temp();// Note: 750msec of sleep is embedded in this function while waiting for data!
//  ds18b20_TEMP_degC =(float)ds18b20_TEMP_Raw*0.0625; //many 12 bit sensors use this same calculation
//  #ifdef ECHO_TO_SERIAL
//  Serial.print(F("DS18b20 Temp is: "));Serial.println(ds18b20_TEMP_degC); 
//  #endif
//#endif
//digitalWrite(GREEN_PIN, LOW);



// find the average for each set of readings
double AVG_ACCEL_READING[3];
double AVG_MAG_READING[3];

loop_readings(AVG_ACCEL_READING, AVG_MAG_READING, 9);

double TILT_ANGLE = calculate_tilt_angle(AVG_ACCEL_READING);
double VELOCITY = calculate_velocity(TILT_ANGLE);
double DIRECTION = calculate_direction(AVG_MAG_READING, AVG_ACCEL_READING);
Serial.print("\n ------------------------ \n");

//========================================================
//Read Light Level with indicator LED color channels 
// Modfied from  //https://playground.arduino.cc/Learning/LEDSensor  I added PIND for speed
// An explaination of the reverse-bias LED reading technique https://www.sparkfun.com/news/2161
// these logarithmic 'discharge time' readings get smaller as the amount of light increases
//
//#ifdef readRedLED 
//uint32_t redLEDreading=readRedLEDchannel(); //call the function which reads the RED led channel
//  #ifdef ECHO_TO_SERIAL
//   Serial.print(F("RedLED= "));Serial.print(redLEDreading);Serial.flush();
//  #endif
//#endif  //readRedLED 
//
//#ifdef readGreenLED 
//uint32_t greenLEDreading=readGreenLEDchannel(); //call the function which reads the RED led channel
//  #ifdef ECHO_TO_SERIAL
//   Serial.print(F("GreenLED= "));Serial.print(greenLEDreading);Serial.flush();
//  #endif
//#endif  //readGreenLED 
//
//#ifdef readBlueLED 
//uint32_t blueLEDreading=readBlueLEDchannel(); //call the function which reads the RED led channel
//  #ifdef ECHO_TO_SERIAL
//   Serial.print(F("BlueLED= "));Serial.print(blueLEDreading);Serial.flush();
//  #endif
//#endif  //readBlueLED 
// 
//pinMode(RED_PIN,INPUT_PULLUP); //indicate SD saving
  
// ========== Pre SD saving battery checks ==========
#if defined (unregulated2xLithiumAA) || defined(ECHO_TO_SERIAL) 
  int preSDsaveBatterycheck=getRailVoltage(); //If you are running from raw battery power (with no regulator) VccBGap IS the battery voltage
#else  // #ifdef voltageRegulated:
  analogReference(DEFAULT);analogRead(BatteryPin); delay(5);  //throw away the first reading when using high impedance voltage dividers!
  floatbuffer = float(analogRead(BatteryPin));
  floatbuffer = (floatbuffer+0.5)*(3.3/1024.0)*4.030303; // 4.0303 = (Rhigh+Rlow)/Rlow for a 10M/3.3M voltage divider combination
  int preSDsaveBatterycheck=int(floatbuffer*1000.0);
#endif
if (preSDsaveBatterycheck < (systemShutdownVoltage+safetyMargin4SDsave+50)) {  //extra 50 thrown in due to 1.1vref uncertainty
    error_shutdown(); //shut down the logger because the voltage is too low for SD saving
}

//========== If battery is OK then it's safe to write the data ===========
// the order of variables being written to the file here should match the text 
// you entered in the dataCollumnLabels[] variable at the start of the code:

    file.open(FileName, O_WRITE | O_APPEND); // open the file for write at end
    file.print(TimeStamp);
    file.print(","); 
    file.print(BatteryReading);
    file.print(","); 
    file.print(safetyMargin4SDsave);
    file.print(",");  
//    file.print(rtc_TEMP_degC); 
//    file.print(",");   
//    file.print(analogPinReading); 
//    file.print(",");   
    file.print(DIRECTION); 
    file.print(",");   
    file.print(TILT_ANGLE); 
    file.print(",");   
       file.print(VELOCITY); 
    file.print(","); 
    for (int i = 0; i <3; i++){
      file.print(AVG_ACCEL_READING[i]);
      file.print(",");
    } 
    for (int j = 0; j <3; j++){
      file.print(AVG_MAG_READING[j]);
      file.print(",");
    }
    
//#ifdef TS_DS18B20
//    file.print(ds18b20_TEMP_Raw);
//    file.print(",");
//    file.print(ds18b20_TEMP_degC,3);
//    file.print(",");

    //OR if you start running out of program memory:
    //char stringBuffer[8]; 
    //stringBuffer[0] = '\0'; //empties stringBuffer by making first character a 'null' value
    //dtostrf((ds18b20_TEMP_degC),7,3,stringBuffer); //7 characters total, with 3 ASCII characters after the decimal point.
    //file.print(stringBuffer); //dtostrf saves exactly the right number of float digits without taking alot of progmem space
    
//#endif
   
#ifdef readRedLED 
    file.print(redLEDreading);
    file.print(",");
#endif 
#ifdef readGreenLED   
    file.print(greenLEDreading);
    file.print(","); 
#endif 
#ifdef readBlueLED   
    file.print(blueLEDreading);
    file.print(","); 
#endif 
    file.println(); 
    file.close();

//========== POST SD saving battery check ===========
//the SD card can pull up to 200mA, and so a more representative battery reading is one taken AFTER this load

  #if defined (unregulated2xLithiumAA) || defined(ECHO_TO_SERIAL) 
  BatteryReading=getRailVoltage(); //If you are running from raw battery power (with no regulator) VccBGap IS the battery voltage
  #else  // #ifdef voltageRegulated:
  analogReference(DEFAULT);analogRead(BatteryPin); delay(5);  //throw away the first reading when using high impedance voltage dividers!
  floatbuffer = float(analogRead(BatteryPin));
  floatbuffer = (floatbuffer+0.5)*(3.3/1024.0)*4.030303; // 4.0303 = (Rhigh+Rlow)/Rlow for a 10M/3.3M voltage divider combination
  BatteryReading=int(floatbuffer*1000.0);
  #endif

//Note: SD card controllers sometimes generate "internal housekeeping events" that draw MUCH more power from the batteries than normal data saves
//so the value in SDsaveVoltageDelta usually increases after these occasional 'really big' power drain events
//that delta also increases as your batteries run down, AND if the temperature falls low enough to reduce the battery voltage
if ((preSDsaveBatterycheck-BatteryReading)>safetyMargin4SDsave) {
  safetyMargin4SDsave= preSDsaveBatterycheck-BatteryReading;
  }
if (BatteryReading < systemShutdownVoltage) { 
    error_shutdown(); //shut down the logger if you get a voltage reading below the cut-off
  }
digitalWrite(RED_PIN, LOW);  // SD saving is over
pinMode(BLUE_PIN,INPUT_PULLUP); // BLUE to indicate RTC events

// OPTIONAL debugging output: only if ECHO_TO_SERIAL is defined
#ifdef ECHO_TO_SERIAL
    Serial.print("Data Saved: "); 
    Serial.print(TimeStamp);
    Serial.print(","); 
    Serial.print(BatteryReading);
    Serial.print(",");  
    Serial.print(safetyMargin4SDsave);
    Serial.print(", ");    
    Serial.print(analogPinReading);
    Serial.println(","); Serial.flush();
#endif
  
//============Set the next alarm time =============
Alarmhour = now.hour();
Alarmminute = now.minute() + SampleIntervalMinutes;
Alarmday = now.day();

// check for roll-overs
if (Alarmminute > 59) { //error catching the 60 rollover!
  Alarmminute = 0;
  Alarmhour = Alarmhour + 1;
  if (Alarmhour > 23) {
    Alarmhour = 0;
    // put ONCE-PER-DAY code here -it will execute on the 24 hour rollover
  }
}
// then set the alarm
RTC.setAlarm1Simple(Alarmhour, Alarmminute);
RTC.turnOnAlarm(1);
if (RTC.checkAlarmEnabled(1)) {
  //you would comment out most of this message printing
  //if your logger was actually being deployed in the field
  
#ifdef ECHO_TO_SERIAL
  Serial.print(F("Alarm Enabled! Going to sleep for :"));
  Serial.print(SampleIntervalMinutes);
  Serial.println(F(" minute(s)")); // println adds a carriage return
  Serial.flush();//waits for buffer to empty
#endif
}

digitalWrite(GREEN_PIN, LOW);digitalWrite(RED_PIN, LOW);digitalWrite(BLUE_PIN, LOW);
  
//=======================================================================
// NOW sleep the logger and wait for next RTC wakeup alarm on pin D2
//=======================================================================
// do-while loop keeps the processor trapped until clockInterrupt is set to true
// if 'anything else' wakes the logger then it just goes right back to sleep

  clockInterrupt = false;
  bitSet(EIFR,INTF0); // clears any old 'EMI noise triggers' from the Interrupt0 FLAG register
  
  do { 
         attachInterrupt(0, rtcISR, LOW);// Enable interrupt on pin2 & attach it to rtcISR function:
           LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_ON); // the RTC alarm wakes the processor from this sleep
         detachInterrupt(0); // immediately disable the interrupt on waking
    
  }while(clockInterrupt == false); // if rtc flag is still false then go back to sleep 
 
  EIFR=EIFR; // this memmory register command clears leftover events from ‘BOTH’ d2&d3 hardware interrupts

// We set the clockInterrupt in the ISR, deal with that now:
if (clockInterrupt) {
    if (RTC.checkIfAlarm(1)) {   //Is the RTC alarm still on?
      RTC.turnOffAlarm(1);       //then turn it off.
    }
clockInterrupt = false;   //reset the interrupt flag to false
}// terminates:  if (clockInterrupt)
  
//now go back to the start of the MAIN loop and start the cycle over again
//====================================================================================================
}   //   terminator the MAIN LOOP
//=====================================================================================================


//====================================================================================
// Stand alone functions called from the main loop:
//====================================================================================
// This is the Interrupt subroutine that only executes when the RTC alarm goes off:
void rtcISR() {                      //called from attachInterrupt(0, rtcISR, LOW);
    clockInterrupt = true;
  }
//====================================================================================
void clearClockTrigger()   // from http://forum.arduino.cc/index.php?topic=109062.0
{
  Wire.beginTransmission(0x68);   //Tell devices on the bus we are talking to the DS3231
  Wire.write(0x0F);               //Tell the device which address we want to read or write
  Wire.endTransmission();         //Before you can write to and clear the alarm flag you have to read the flag first!
  Wire.requestFrom(0x68,1);       //Read one byte
  bytebuffer1=Wire.read();        //In this example we are not interest in actually using the byte
  Wire.beginTransmission(0x68);   //Tell devices on the bus we are talking to the DS3231 
  Wire.write(0x0F);               //Status Register: Bit 3: zero disables 32kHz, Bit 7: zero enables the main oscilator
  Wire.write(0b00000000);         //Bit1: zero clears Alarm 2 Flag (A2F), Bit 0: zero clears Alarm 1 Flag (A1F)
  Wire.endTransmission();
  clockInterrupt=false;           //Finally clear the flag we used to indicate the trigger occurred
}
//====================================================================================
// Enable Battery-Backed Square-Wave Enable on the DS3231 RTC module: 
/* Bit 6 (Battery-Backed Square-Wave Enable) of DS3231_CONTROL_REG 0x0E, can be set to 1 
 * When set to 1, it forces the wake-up alarms to occur when running the RTC from the back up battery alone. 
 * [note: This bit is usually disabled (logic 0) when power is FIRST applied]
 */
  void enableRTCAlarmsonBackupBattery(){
  Wire.beginTransmission(DS3231_I2C_ADDRESS);// Attention RTC 
  Wire.write(DS3231_CONTROL_REG);            // move the memory pointer to CONTROL_REG
  Wire.endTransmission();                    // complete the ‘move memory pointer’ transaction
  Wire.requestFrom(DS3231_I2C_ADDRESS,1);    // request data from register
  byte resisterData = Wire.read();           // byte from registerAddress
  bitSet(resisterData, 6);                   // Change bit 6 to a 1 to enable
  Wire.beginTransmission(DS3231_I2C_ADDRESS);// Attention RTC
  Wire.write(DS3231_CONTROL_REG);            // target the register
  Wire.write(resisterData);                  // put changed byte back into CONTROL_REG
  Wire.endTransmission();
  }
  
//========================================================================================
void error_shutdown(){
    digitalWrite(GREEN_PIN, LOW);digitalWrite(RED_PIN, LOW);digitalWrite(BLUE_PIN, LOW);
    // spend some time flashing red indicator light on error before shutdown!
    for (int CNTR = 0; CNTR < 100; CNTR++) { 
    pinMode(RED_PIN,INPUT_PULLUP);
    LowPower.powerDown(SLEEP_250MS, ADC_OFF, BOD_ON);
    digitalWrite(RED_PIN, LOW);
    LowPower.powerDown(SLEEP_250MS, ADC_OFF, BOD_ON);
  }
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_ON); //BOD is left on here to protect the processor
}

//====================================================================================
int getRailVoltage()    // modified from http://forum.arduino.cc/index.php/topic,38119.0.html
{
  int result; // gets passed back to main loop
  int value;  // temp variable for the conversion to millivolts
    // ADC configuration command for ADC on 328p based Arduino boards  // REFS1 REFS0  --> 0 1, AVcc internal ref. // MUX3 MUX2 MUX1 MUX0 -->1110 sets 1.1V bandgap
    ADMUX = (0 << REFS1) | (1 << REFS0) | (0 << ADLAR) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1) | (0 << MUX0);
    // Note: changing the ADC reference from the (default) 3.3v rail to internal 1.1V bandgap can take up to 10 msec to stabilize
    for (int i = 0; i < 7; i++) { // loop several times so the capacitor connected to the aref pin can discharge down to the 1.1v reference level
    ADCSRA |= _BV( ADSC );                     // Start an ADC conversion
    while ( ( (ADCSRA & (1 << ADSC)) != 0 ) ); // makes the processor wait for ADC reading to complete
    value = ADC; // value = the ADC reading
    delay(1);    // delay time for capacitor on Aref to discharge
    } // for(int i=0; i <= 5; i++)
    ADMUX = bit (REFS0) | (0 & 0x07); analogRead(A0); // post reading cleanup: select input channel A0 + re-engage the default rail as Aref
    result = (((InternalReferenceConstant) / (long)value)); //scale the ADC reading into milliVolts  
    return result;
  
}  // terminator for getRailVoltage() function

//====================================================================================
// DS18B20  ONE WIRE TEMPERATURE reading function  https://www.best-microcontroller-projects.com/ds18b20.html
// this function from library at http://www.pjrc.com/teensy/td_libs_OneWire.html
// also see Dallas Temperature Control library by Miles Burton: http://milesburton.com/Dallas_Temperature_Control_Library

#if defined(TS_DS18B20)
int readDS18B20Temp()
{
  byte data[2]; //byte data[12]; there are more bytes of data to be read...
  ds.reset();
  ds.select(addr);
  ds.write(0x44); // start conversion, read temperature & store it in the scratchpad
  LowPower.powerDown(SLEEP_500MS, ADC_OFF, BOD_ON); // Put the Arduino processor to sleep
  LowPower.powerDown(SLEEP_250MS, ADC_OFF, BOD_ON); // while the DS18b20 gathers it's reading
  delay(5); //regulator stabilization after uC wakeup
  byte present = ds.reset();
  ds.select(addr);
  ds.write(0xBE); // Read Scratchpad
  for (int i = 0; i < 2; i++)
  {
    data[i] = ds.read();
  }
  byte MSB = data[1];
  byte LSB = data[0];
  int tempRaw = ((MSB << 8) | LSB); //using two's compliment //TEMP_degC = tempRaw / 16;
  return tempRaw;
}
#endif

//The time needed between the CONVERT_T command and the READ_SCRATCHPAD command has to be at least
//750 millisecs (but can be shorter if using a D18B20 set to resolution < 12 bits)
//if you start getting "85" all the time you did not wait long enough
// One quirk of this sensor is that Dallas choose a value inside the valid range as the powerup default.
// The DS18b20 sensor will reset to 85C on power up, and this can happen when doing a long conversion and power falls too low.
// The sensor appears on the bus, since it’s able to use parasitic power, but as soon as you try to read a temperature it will fall off the bus.
// The sensor might also report 85C if the temperature is retrieved but the sensor has not been commanded

/* from Chris Shucksmith:  http://www.jon00.me.uk/onewireintro.shtml
 * "If you intend to have a large 1-wire network, it is important that you design the network correctly, otherwise you will have problems with 
 * timing/reflection issues and loss of data. For very small networks, it is possible to connect each sensor in a star or radial arrangement. 
 * This means that each sensor is connected via its own cable back to a central point and then connected to the 1-wire to serial adapter. 
 * However, it is strongly recommend that you connect each sensor to a single continuous cable which loops from sensor to sensor in turn (daisy chain). 
 * This will reduce potential misreads due to reflections in the cable. Each sensor should have a maximum of 50mm (2") of cable connected off the 
 * main highway. Even using this method, connecting more than 10-15 sensors will still cause problems due to loading of the data bus. 
 * To minimise this effect, always place a 100-120 ohm resistor in the data leg of each sensor before connecting to the network."
 */

//====================================================================================
// Separate functions to read light Level with the 3 RGB LED color channels
//===================================================================================================
// Modfied from  //https://playground.arduino.cc/Learning/LEDSensor  I added PIND for speed
// An explaination of the reverse-bias LED reading technique at https://www.sparkfun.com/news/2161
// the readings get smaller as the amount of light increases and the response is logarithmic

// Note: do not try to sleep the processor during the counting loop - when sleeping, any GPIO that 
// is not used an an interrupt input has its input buffer disconnected from the pin and is clamped LOW by the 'sleep' MOSFET.

#ifdef readRedLED
//==========================
uint32_t readRedLEDchannel(){
  power_all_disable(); //disable all the chip peripherals to reduce spurious interrupts
  uint32_t loopTime;
  byte gndPin = (1 << LED_GROUND_PIN);

  //'discharge'all LED channels before reading
  digitalWrite(LED_GROUND_PIN,LOW); pinMode(LED_GROUND_PIN,OUTPUT);
  pinMode(BLUE_PIN,INPUT_PULLUP); pinMode(GREEN_PIN,INPUT_PULLUP);pinMode(RED_PIN,INPUT_PULLUP);
  digitalWrite(BLUE_PIN,LOW); digitalWrite(GREEN_PIN,LOW);digitalWrite(RED_PIN,LOW); 
  
  pinMode(RED_PIN,OUTPUT);
  pinMode(LED_GROUND_PIN,INPUT_PULLUP); //Reverses Polarity to charge LED's internal capacitance
  _delay_us(24); //alternative to delayMicroseconds()//calls to __builtin_avr_delay_cycles(), which are compiled into delay loops.
  digitalWrite(LED_GROUND_PIN,LOW);
  
  for (loopTime = 0; loopTime < 1200000; loopTime++) { // Counts how long it takes the LED to fall to the logic 0 voltage level
    if ((PIND & gndPin) == 0) break; // equivalent to: "if (digitalRead(LED_GROUND_PIN)=LOW) stop looping"
    //PIND uses port manipulation so executes much faster than digitalRead-> increasing the resolution of the sensor
  }
  
  power_all_enable();
  pinMode(RED_PIN,INPUT); //not needed all pins in input now
  pinMode(LED_GROUND_PIN,OUTPUT); //back to normal 'ground' pin operation
   return loopTime;
}  // terminator
#endif readRedLED

#ifdef readGreenLED  //this function READs Green channel of 3-color indicator LED  
//=============================
uint32_t readGreenLEDchannel(){
  power_all_disable(); // stops All TIMERS, ADC, TWI, SPI, USART 
  uint32_t loopTime;//uint32_t max of 4 294 967 295
  byte gndPin = (1 << LED_GROUND_PIN);
  
  //'discharge' all LED channels before reading
  digitalWrite(LED_GROUND_PIN,LOW);pinMode(LED_GROUND_PIN,OUTPUT);
  pinMode(BLUE_PIN,INPUT_PULLUP);pinMode(RED_PIN,INPUT_PULLUP);pinMode(GREEN_PIN,INPUT_PULLUP);
  digitalWrite(BLUE_PIN,LOW);digitalWrite(RED_PIN,LOW);digitalWrite(GREEN_PIN,LOW);
  
  pinMode(GREEN_PIN,OUTPUT);//Reverse Polarity on the color channel being read
  pinMode(LED_GROUND_PIN,INPUT_PULLUP); //to charge LED's internal capacitance
  _delay_us(24); //alternative to delayMicroseconds()//calls to __builtin_avr_delay_cycles(), which are compiled into delay loops.
  digitalWrite(LED_GROUND_PIN,LOW);
  for (loopTime = 0; loopTime < 1200000; loopTime++) { // on my led 1.2M goes to approximately 0 LUX
    if ((PIND & gndPin) == 0) break; // equivalent to: "if (digitalRead(LED_GROUND_PIN)=LOW) stop looping"
    //this loop takes almost exactly one microsecond to cycle @ 8mhz
  }
   power_all_enable();
   pinMode(GREEN_PIN,INPUT);
   pinMode(LED_GROUND_PIN,OUTPUT); //back to normal 'ground' pin operation
   return loopTime;
}// terminator for readGreenLEDchannel() function

#endif  //if readGreenLED

#ifdef readBlueLED //this function READs BLUE channel of 3-color indicator LED
//============================
uint32_t readBlueLEDchannel(){
  
  power_all_disable(); // stops All TIMERS, ADC, TWI, SPI, USART  
  uint32_t loopTime = 0;
  uint64_t startTime = 0;
  byte gndPin = (1 << LED_GROUND_PIN);

  //'discharge' all LED channels before reading
  digitalWrite(LED_GROUND_PIN,LOW);pinMode(LED_GROUND_PIN,OUTPUT);
  pinMode(BLUE_PIN,INPUT_PULLUP);pinMode(GREEN_PIN,INPUT_PULLUP);pinMode(RED_PIN,INPUT_PULLUP); 
  digitalWrite(GREEN_PIN,LOW);digitalWrite(RED_PIN,LOW);digitalWrite(BLUE_PIN,LOW);
  
  pinMode(BLUE_PIN,OUTPUT); 
  pinMode(LED_GROUND_PIN,INPUT_PULLUP); //Reverses Polarity to charge LED's internal capacitance
  _delay_us(24); //alternative to delayMicroseconds()//calls to __builtin_avr_delay_cycles(), which are compiled into delay loops.
  digitalWrite(LED_GROUND_PIN,LOW);
  for (loopTime = 0; loopTime < 1200000; loopTime++) { //loopTime prevents us from counting forever if pin does not fall
    if ((PIND & gndPin) == 0) break; // equivalent to: "if (digitalRead(LED_GROUND_PIN)=LOW) stop looping"
    //this loop takes almost exactly one microsecond to cycle @ 8mhz
  }
   power_all_enable();    //re-enable our peripherals
   pinMode(BLUE_PIN,INPUT);
   pinMode(LED_GROUND_PIN,OUTPUT); //back to normal 'ground' pin operation
   return loopTime;
}  // terminator for readBlueLEDchannel() function
#endif //readBlueLED

//================================================================================================
//  SIGNAL PROCESSING FUNCTIONS
//================================================================================================
/* 
This median3 filter is pretty good at getting rid of single NOISE SPIKES from flakey sensors
(It is better than any low pass filter, moving average, weighted moving average, etc. 
IN TERMS OF ITS RESPONSE TIME and its ability  to ignore such single-sample noise spike outliers. 
The median-of-3 requires very little CPU power, and is quite fast.
*/
// pass three separate positive integer readings into this filter:
// for more on bitwise xor operator see https://www.arduino.cc/reference/en/language/structure/bitwise-operators/bitwisexor/  
int median_of_3( int a, int b, int c ){  // created by David Cary 2014-03-25
    int the_max = max( max( a, b ), c );
    int the_min = min( min( a, b ), c );
    int the_median = the_max ^ the_min ^ a ^ b ^ c;
    return( the_median );
}                                        // teriminator for median_of_3

/*
// for continuous readings, drop oldest int value and shift in latest reading before calling this function:
oldest = recent;
recent = newest;
newest = analogRead(A0);
*/

//================================================================================================
//  SENSOR READING FUNCTIONS
//================================================================================================
double read_magnetometer(double *MAG_READING){
   // CHANGE COMPASS
   compass.read();
   MAG_READING[0] = compass.m.x;
   MAG_READING[1] = compass.m.y;
   MAG_READING[2] = compass.m.z;

   Serial.print((String) "Mag Raw X: " + MAG_READING[0] + "  ");
   Serial.print((String) "Mag Raw Y: " + MAG_READING[1] + "  ");
   Serial.print((String) "Mag Raw Z: " + MAG_READING[2] + "\n");
}

double calculate_direction(double *MAG_READING, double *ACCEL_READING){
  const float alpha = 0.15;
  float fXm = 0;
  float fYm = 0;
  float fZm = 0;
  float pitch, pitch_print, roll, roll_print, Heading, Xm_off, Ym_off, Zm_off, Xm_cal, Ym_cal, Zm_cal, fXm_comp, fYm_comp;

  // INPUT HERE: take these values from the Combined bias(b): field in Magneto
  float combined_bias_x = -442.469658;
  float combined_bias_y = 18761.954610;
  float combined_bias_z = -2437.724224;

  // INPUT HERE; take these values from the "Correction for combined scale factors..." field in Magneto
  float correction_xcal_xoff = 0.001020;
  float correction_xcal_yoff = 0.000018;
  float correction_xcal_zoff = 0.000028;
  float correction_ycal_xoff = 0.000018;
  float correction_ycal_yoff = 0.001033;
  float correction_ycal_zoff = 0.000005;
  float correction_zcal_xoff = 0.000028;
  float correction_zcal_yoff = 0.000005;
  float correction_zcal_zoff = 0.001041;
 
  // Magnetometer calibration
  Xm_off = MAG_READING[0]*(100000.0/1100.0) - combined_bias_x;
  Ym_off = MAG_READING[1]*(100000.0/1100.0) - combined_bias_y;
  Zm_off = MAG_READING[2]*(100000.0/980.0 ) - combined_bias_z;

  Xm_cal =  correction_xcal_xoff*Xm_off + correction_xcal_yoff*Ym_off + correction_xcal_zoff*Zm_off;
  Ym_cal =  correction_ycal_xoff*Xm_off + correction_ycal_yoff*Ym_off + correction_ycal_zoff*Zm_off;
  Zm_cal =  correction_zcal_xoff*Xm_off + correction_zcal_yoff*Ym_off + correction_zcal_zoff*Zm_off;

  // Low-Pass filter magnetometer
  fXm = Xm_cal * alpha + (fXm * (1.0 - alpha));
  fYm = Ym_cal * alpha + (fYm * (1.0 - alpha));
  fZm = Zm_cal * alpha + (fZm * (1.0 - alpha));

  // Pitch and roll
  roll  = atan2(ACCEL_READING[1], sqrt(ACCEL_READING[0]*ACCEL_READING[0] + ACCEL_READING[2]*ACCEL_READING[2]));

//  pitch = atan2(-ACCEL_READING[2], sqrt(ACCEL_READING[1]*ACCEL_READING[1] + ACCEL_READING[0]*ACCEL_READING[0]));
  pitch = atan2(ACCEL_READING[0], sqrt(ACCEL_READING[1]*ACCEL_READING[1] + ACCEL_READING[2]*ACCEL_READING[2]));
 
  roll_print = roll*180.0/M_PI;
  pitch_print = pitch*180.0/M_PI;
 
  // Tilt compensated magnetic sensor measurements
  fXm_comp = fXm*cos(pitch)+fZm*sin(pitch);
  fYm_comp = fXm*sin(roll)*sin(pitch)+fYm*cos(roll)-fZm*sin(roll)*cos(pitch);
 
  // Arctangent of y/x
  Heading = (atan2(fYm_comp,fXm_comp)*180.0)/M_PI;
  if (Heading < 0)
  Heading += 360;
 
  Serial.print("Pitch (X): "); Serial.print(pitch_print); Serial.print("  ");
  Serial.print("Roll (Y): "); Serial.print(roll_print); Serial.print("  ");
  Serial.print("Heading: "); Serial.println(Heading);
  return Heading;
}



//-------------------------

double read_accelerometer(double *ACCEL_READING){
  sensors_event_t event1;
  accel.getEvent(&event1);

  /* Display the results (acceleration is measured in m/s^2) */
  Serial.print("Acc Raw X: ");
  Serial.print(event1.acceleration.x);
  Serial.print("  ");
  Serial.print("Acc Raw  Y: ");
  Serial.print(event1.acceleration.y);
  Serial.print("  ");
  Serial.print("Acc Raw  Z: ");
  Serial.print(event1.acceleration.z);
  Serial.print("  ");
  Serial.println("m/s^2");

  ACCEL_READING[0] = event1.acceleration.x;
  ACCEL_READING[1] = event1.acceleration.y;
  ACCEL_READING[2] = event1.acceleration.z;
}


// calculates tilt angle based on accelerometer reading
double calculate_tilt_angle(double *ACCEL_READING){
  // set constants
  const double g = 9.216;
  const double ACCEL_0[3] = {g, -0.133, 2.302}; // a0 acceleration vector (hanging sensor)

  // calculate theta
  double a_numer = ACCEL_READING[0]*ACCEL_0[0] + ACCEL_READING[1]*ACCEL_0[1] + ACCEL_READING[2]*ACCEL_0[2];
  double a_denom_reading = sqrt(pow(ACCEL_READING[0],2) + pow(ACCEL_READING[1],2) + pow(ACCEL_READING[2],2));
  double a_denom_0 = sqrt(pow(ACCEL_0[0],2) + pow(ACCEL_0[1],2) + pow(ACCEL_0[2],2));
  double a_term = a_numer / (a_denom_reading * a_denom_0);
  double theta = acos(a_term);
  
  Serial.print("Theta: ");
  Serial.print(theta*180/PI);
  Serial.print("\n");
  return theta*180/PI;
}

double calculate_velocity(double tilt_angle){
  double velocity = 0.0001*(pow(tilt_angle,2)) - 0.0007*tilt_angle + 0.3404;
  
  Serial.print("Velocity: ");
  Serial.print(velocity);
  Serial.print("\n");
  
  return velocity;
}
double loop_readings(double *AVG_ACCEL_READING, double *AVG_MAG_READING, int n){
  for (int i = 0; i < n; i++) {
    double ACCEL_READING[3];
    read_accelerometer(ACCEL_READING);
    double MAG_READING[3];
    read_magnetometer(MAG_READING);
    
    for (int i = 0; i < 3; i++){
      AVG_ACCEL_READING[i] += ACCEL_READING[i];
      AVG_MAG_READING[i] += MAG_READING[i];
    }
    delay(1000);
  }

  for (int i=0; i<3; i++){
    AVG_ACCEL_READING[i] /= n;
    AVG_MAG_READING[i] /= n;
  }
}

//================================================================================================
// NOTE: for more complex signal filtering, look into the digitalSmooth function with outlier rejection
// by Paul Badger at  http://playground.arduino.cc/Main/DigitalSmooth  works well with acclerometers, etc
