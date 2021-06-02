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

## Calibration
* Download the [Magneto application](https://sites.google.com/site/sailboatinstruments1/home)
* [Calculate the magnetic field](https://www.ngdc.noaa.gov/geomag/calculators/magcalc.shtml#igrfwmm) using your latitude and longitude (should be the first value under "Total Field")
* In the repository, go to reeferees > calibration > calibrate_mag and open up `calibrate_mag` Arduino file
* Upload the code to the Arduino and open up Tools > Serial Monitor
* Keep moving around + rotating the data logger while the code is running. You should see 3 columns of values in the Serial Monitor
* After about 5 minutes, copy and paste the contents of the Serial Monitor and copy and save it into a .txt file
* Open up Magneto
  * Enter in the Norm of Magnetic or Gravitational field (the one you calculated using latitude and longitude)
  * Upload your .txt file
  * Press "Calibrate"
* Using the x, y, and z values under the "Combined bias (b)" field in Magneto:
  * Search for `// INPUT HERE: take these values from the Combined bias(b): field in Magneto` in saveToSD.ino and change those corresponding values with the ones you got from Magneto
  * Enter the values left to right (x is on the left, z is on the right)
* Using the 9 values from the "Correction for combined scale factors"
  * Search for `// INPUT HERE; take these values from the "Correction for combined scale factors..." field in Magneto`in saveToSD.ino and change those corresponding values with the ones you got from Magneto
  * Enter the values going from left to right, top to down (top left first, bottom right last)
