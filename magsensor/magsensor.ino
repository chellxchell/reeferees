#include <Adafruit_LSM303_Accel.h>
#include <Adafruit_LSM303DLH_Mag.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <math.h>       /* atan */
#include <LSM303.h>
LSM303 compass;

/* Assign a unique ID to this sensor at the same time */
Adafruit_LSM303_Accel_Unified accel = Adafruit_LSM303_Accel_Unified(54321);
Adafruit_LSM303DLH_Mag_Unified mag = Adafruit_LSM303DLH_Mag_Unified(12345);

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

  // ------------------
//  mag.getEvent(&event);
  compass.read();
 
  /* Display the results (magnetic vector values are in micro-Tesla (uT)) */
  Serial.print("Raw Mag X: ");
  Serial.print(compass.m.x);
  Serial.print("  ");
  Serial.print("Raw Mag Y: ");
  Serial.print(compass.m.y);
  Serial.print("  ");
  Serial.print("Raw Mag Z: ");
  Serial.print(compass.m.z);
  Serial.print("  ");
  Serial.println("uT");

  double MAG_READING[] = {compass.m.x, compass.m.y, compass.m.z};
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
//  fXm_comp = fXm*cos(pitch)+fZm*sin(pitch);
//  fYm_comp = fXm*sin(roll)*sin(pitch)+fYm*cos(roll)-fZm*sin(roll)*cos(pitch);
  fXm_comp = -fZm*cos(pitch)+fXm*sin(pitch);
  fYm_comp = -fZm*sin(roll)*sin(pitch)+fYm*cos(roll)-fXm*sin(roll)*cos(pitch);

 
  // Arctangent of y/x
  Heading = (atan2(fYm_comp,fXm_comp)*180.0)/M_PI;
  if (Heading < 0)
  Heading += 360;
 
  Serial.print("Pitch (X): "); Serial.print(pitch_print); Serial.print("  ");
  Serial.print("Roll (Y): "); Serial.print(roll_print); Serial.print("  ");
  Serial.print("Heading: "); Serial.println(Heading);
  return Heading;
}
