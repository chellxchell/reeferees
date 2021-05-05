#include <Adafruit_LSM303_Accel.h>
#include <Adafruit_LSM303DLH_Mag.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <math.h>       /* atan */

/* Assign a unique ID to this sensor at the same time */
Adafruit_LSM303_Accel_Unified accel = Adafruit_LSM303_Accel_Unified(54321);
Adafruit_LSM303DLH_Mag_Unified mag = Adafruit_LSM303DLH_Mag_Unified(12345);

void displayAccSensorDetails(void) {
  sensor_t sensor;
  accel.getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.print("Sensor:       ");
  Serial.println(sensor.name);
  Serial.print("Driver Ver:   ");
  Serial.println(sensor.version);
  Serial.print("Unique ID:    ");
  Serial.println(sensor.sensor_id);
  Serial.print("Max Value:    ");
  Serial.print(sensor.max_value);
  Serial.println(" m/s^2");
  Serial.print("Min Value:    ");
  Serial.print(sensor.min_value);
  Serial.println(" m/s^2");
  Serial.print("Resolution:   ");
  Serial.print(sensor.resolution);
  Serial.println(" m/s^2");
  Serial.println("------------------------------------");
  Serial.println("");
  delay(500);
}
void displayMagSensorDetails(void) {
  sensor_t sensor;
  mag.getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.print("Sensor:       ");
  Serial.println(sensor.name);
  Serial.print("Driver Ver:   ");
  Serial.println(sensor.version);
  Serial.print("Unique ID:    ");
  Serial.println(sensor.sensor_id);
  Serial.print("Max Value:    ");
  Serial.print(sensor.max_value);
  Serial.println(" uT");
  Serial.print("Min Value:    ");
  Serial.print(sensor.min_value);
  Serial.println(" uT");
  Serial.print("Resolution:   ");
  Serial.print(sensor.resolution);
  Serial.println(" uT");
  Serial.println("------------------------------------");
  Serial.println("");
  delay(500);
}
 
void setup(void) {
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
  displayAccSensorDetails();

// --------
Serial.println("Magnetometer Test");
  Serial.println("");
 
  /* Enable auto-gain */
  mag.enableAutoRange(true);
 
  /* Initialise the sensor */
  if (!mag.begin()) {
    /* There was a problem detecting the LSM303 ... check your connections */
    Serial.println("Ooops, no LSM303 detected ... Check your wiring!");
    while (1)
      ;
  }
 
  /* Display some basic information on this sensor */
  displayMagSensorDetails();
//---------
  accel.setRange(LSM303_RANGE_4G);
  Serial.print("Range set to: ");
  lsm303_accel_range_t new_range = accel.getRange();
  switch (new_range) {
  case LSM303_RANGE_2G:
    Serial.println("+- 2G");
    break;
  case LSM303_RANGE_4G:
    Serial.println("+- 4G");
    break;
  case LSM303_RANGE_8G:
    Serial.println("+- 8G");
    break;
  case LSM303_RANGE_16G:
    Serial.println("+- 16G");
    break;
  }

  accel.setMode(LSM303_MODE_NORMAL);
  Serial.print("Mode set to: ");
  lsm303_accel_mode_t new_mode = accel.getMode();
  switch (new_mode) {
  case LSM303_MODE_NORMAL:
    Serial.println("Normal");
    break;
  case LSM303_MODE_LOW_POWER:
    Serial.println("Low Power");
    break;
  case LSM303_MODE_HIGH_RESOLUTION:
    Serial.println("High Resolution");
    break;
  }
}

void loop(void) {
  /* Get a new sensor event */
  sensors_event_t event;
  accel.getEvent(&event);

  /* Display the results (acceleration is measured in m/s^2) */
  Serial.print("Acc X: ");
  Serial.print(event.acceleration.x);
  Serial.print("  ");
  Serial.print("Acc Y: ");
  Serial.print(event.acceleration.y);
  Serial.print("  ");
  Serial.print("Acc Z: ");
  Serial.print(event.acceleration.z);
  Serial.print("  ");
  Serial.println("m/s^2");
  Serial.print("--------------\n");

  double ACCEL_READING[3];
  ACCEL_READING[0] = event.acceleration.x;
  ACCEL_READING[1] = event.acceleration.y;
  ACCEL_READING[2] = event.acceleration.z;
  calculate_tilt_angle(ACCEL_READING);
  
  /* Delay before the next sample */
  delay(500);

  // -----
  mag.getEvent(&event);
 
  /* Display the results (magnetic vector values are in micro-Tesla (uT)) */
  Serial.print("Raw Mag X: ");
  Serial.print(event.magnetic.x);
  Serial.print("  ");
  Serial.print("Raw Mag Y: ");
  Serial.print(event.magnetic.y);
  Serial.print("  ");
  Serial.print("Raw Mag Z: ");
  Serial.print(event.magnetic.z);
  Serial.print("  ");
  Serial.println("uT");

  double res = 0.0;
  double x = event.magnetic.x, y = event.magnetic.y, z = event.magnetic.z;
  
  if (z > 0){
    res = 180 + (atan(y/abs(z)))*180/PI;
  }
  else if (z < 0){
    if (y > 0){
      res = 360 - (atan(y/abs(z)))*180/PI;
    }
    if (y < 0){
      res = (atan(y/abs(z)))*180/PI;
    }
  }
  else{
    Serial.print("No current");
  }
  Serial.print((String) "Compass Result: " + res);
  Serial.print("\n ------------------------------ \n");
  Serial.print("\n ------------------------------ \n");

  /* Delay before the next sample */
  delay(500);
}

double calculate_tilt_angle(double *ACCEL_READING){
  // set constants
  const double rho = 997.0; // (water density, kg/m3) ρ = 997 kg/m3.
//  const double rho = 1.2041; // (air density for testing, kg/m3) 
  const double Cd = 1.15; // (drag coefficient, no units)
  const double A = inchesToMeters(7.875); // (cross section, meters)
  const double m = 0.5; // CHANGE THIS (mass)
  const double g = 9.81;
  const double ACCEL_0[3] = {g, 0.54, 0.46}; // a0 acceleration vector (hanging sensor)
  const double k = sqrt((rho*Cd*A)/(2*m*g));

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

double inchesToMeters(double in){
  return (in / 39.37);
}
