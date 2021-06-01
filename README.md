# reeferees

## Software Installations
* Make sure you have[git downloaded](https://git-scm.com/downloads). It should be already installed for MacOS, but you need a manual install for Windows)
* Need the [Arduino IDE](https://www.arduino.cc/en/software)
* Install [the UART driver](https://ftdichip.com/drivers/vcp-drivers/). You will need this to connect the Arduino to your computer.

## Installing Libraries
* Accelerometer/Magnetometer libraries
  * Follow [these instructions](https://learn.adafruit.com/lsm303-accelerometer-slash-compass-breakout/coding#install-the-libraries-1512217-3). For the magnetometer, download th e library for the LSM303/LSM303DLHC.
* Manual downloads - for each of these libraries, download them as a .zip file and follow [these instructions](https://www.arduino.cc/en/Guide/Libraries) to install them.
  * https://github.com/pololu/lsm303-arduino/archive/master.zip
  * https://github.com/MrAlvin/RTClib
  * https://github.com/rocketscream/Low-Power
  * https://github.com/greiman/SdFat

## Connect the Arduino
* In the Arduino IDE, in Tools > Board make sure you have "Arduino Pro or Pro Mini" selected
* In Tools > Port make sure you have the correct port selected (you can check in Device Manager)
* In Tools > Processor make sure you have "ATmega328P (3.3V, 8MHz)
* Make sure the wires are connected is with the brown side on the left and the blue on the right (with the ProMini facing closest to you)
* Set the Serial Monitor to 115200 baud

## Set Up The Code
* Clone this repository
  * Open up command prompt and [navigate to the folder](https://www.digitalcitizen.life/command-prompt-how-use-basic-commands/) where you want to save this repository
  * In the command prompt, type in `git clone https://github.com/chellxchell/reeferees.git`
* Go back to the saveToSd file and press "upload" to upload the script to the Arduino
* Go to Tools > Serial Monitor to view the console output
* In File Explorer, navigate to whatever > filepath > reeferees > saveToSD and open up 'saveToSD'
