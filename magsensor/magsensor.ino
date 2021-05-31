#include <Adafruit_LSM303_Accel.h>
#include <Adafruit_LSM303DLH_Mag.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <math.h>       /* atan */
#include <LSM303.h>
LSM303 compass;

const float alpha = 0.15;
float fXm = 0;
float fYm = 0;
float fZm = 0;

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
  Wire.begin();
  compass.init();
  compass.enableDefault();

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

//--------------------------------------------------
  compass.read();
  float pitch, pitch_print, roll, roll_print, Heading, Xm_off, Ym_off, Zm_off, Xm_cal, Ym_cal, Zm_cal, fXm_comp, fYm_comp;

  // INPUT HERE: take these values from the Combined bias(b): field in Magneto
  float combined_bias_x = -4211.532222;
  float combined_bias_y = 31324.106405;
  float combined_bias_z = -9758.227533;

  // INPUT HERE; take these values from the "Correction for combined scale factors..." field in Magneto
  float correction_xcal_xoff = 0.001120;
  float correction_xcal_yoff = 0.000058;
  float correction_xcal_zoff = 0.000027;
  float correction_ycal_xoff = 0.000058;
  float correction_ycal_yoff = 0.001003;
  float correction_ycal_zoff = 0.000013;
  float correction_zcal_xoff = 0.000027;
  float correction_zcal_yoff = 0.000013;
  float correction_zcal_zoff = 0.001300;
  
  // Magnetometer calibration
  Xm_off = compass.m.x*(100000.0/1100.0) - combined_bias_x;
  Ym_off = compass.m.y*(100000.0/1100.0) - combined_bias_y;
  Zm_off = compass.m.z*(100000.0/980.0 ) - combined_bias_z;
  Xm_cal =  correction_xcal_xoff*Xm_off + correction_xcal_yoff*Ym_off + correction_xcal_zoff*Zm_off;
  Ym_cal =  correction_ycal_xoff*Xm_off + correction_ycal_yoff*Ym_off + correction_ycal_zoff*Zm_off;
  Zm_cal =  correction_zcal_xoff*Xm_off + correction_zcal_yoff*Ym_off + correction_zcal_zoff*Zm_off;

  // Low-Pass filter magnetometer
  fXm = Xm_cal * alpha + (fXm * (1.0 - alpha));
  fYm = Ym_cal * alpha + (fYm * (1.0 - alpha));
  fZm = Zm_cal * alpha + (fZm * (1.0 - alpha));

  // Pitch and roll
  roll  = atan2(event.acceleration.y, sqrt(event.acceleration.x*event.acceleration.x + event.acceleration.z*event.acceleration.z));
  pitch = atan2(event.acceleration.x, sqrt(event.acceleration.y*event.acceleration.y + event.acceleration.z*event.acceleration.z));
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
  delay(250);
//--------------------------------------------------
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

  double MAG_READING[] = {event.magnetic.x, event.magnetic.y, event.magnetic.z};
  calculate_direction(MAG_READING,ACCEL_READING);

  /* Delay before the next sample */
  delay(500);
}

double calculate_tilt_angle(double *ACCEL_READING){
  // set constants
  const double g = 9.81;
  const double ACCEL_0[3] = {-g, 0.54, 0.46}; // a0 acceleration vector (hanging sensor)

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

double calculate_direction(double *MAG_READING, double *ACCEL_READING){
  // calculations for this function based on https://www.pololu.com/file/0J434/LSM303DLH-compass-app-note.pdf
  double Mx = MAG_READING[0], My = MAG_READING[1], Mz = MAG_READING[2];
  double Ax = ACCEL_READING[0], Ay = ACCEL_READING[1], Az = ACCEL_READING[2];

  // equation 10
  double rho = asin(-Ax);
  double gamma = asin(Ay/cos(rho));

  // equation 12
  double mag_x = Mx*cos(rho) + Mz*sin(rho);
  double mag_y = Mx*sin(gamma)*sin(rho) + My*cos(gamma) - Mz*sin(gamma)*cos(rho);
  double mag_z = -Mx*cos(gamma)*sin(rho) + My*sin(gamma) + Mz*cos(gamma)*cos(rho);


  double heading = 0.0;

  // equation 13
  if ((mag_x > 0) & (mag_y >= 0)){
    heading = atan(mag_y / mag_x);
  }
  else if (mag_x < 0){
    heading = 180 + atan(mag_y / mag_x);
  }
  else if (mag_x > 0 & mag_y <= 0){
    heading = 360 + atan(mag_y / mag_x);
  }
  else if (mag_x == 0 & mag_y < 0){
    heading = 90;
  }
  else if (mag_x == 0 & mag_y > 0){
    heading = 270;
  }

  Serial.print((String) "Heading: " + heading + "\n");
  return heading;
}
double inchesToMeters(double in){
  return (in / 39.37);
}
