/*	MS5803
 * 	An Arduino library for the Measurement Specialties MS5803 family
 * 	of pressure sensors. This library uses I2C to communicate with the
 * 	MS5803 using the Wire library from Arduino.
 *	
 *	This library only works with the MS5803 2, 5, and 14 bar range model sensors. 
 * 	It DOES NOT work with the other pressure-range models such as the MS5803-30BA 
 * 	or MS5803-01BA. Those models will return incorrect pressure and temperature 
 *	readings if used with this library. See http://github.com/millerlp for
 *	libraries for the other models. 
 *	 
 * 	No warranty is given or implied. You are responsible for verifying that 
 *	the outputs are correct for your sensor. There are likely bugs in
 *	this code that could result in incorrect pressure readings, particularly
 *	due to variable overflows within some pressure ranges. 
 * 	DO NOT use this code in a situation that could result in harm to you or 
 * 	others because of incorrect pressure readings.
 * 	 
 * 	
 * 	Licensed under the GPL v3 license. 
 * 	Please see accompanying LICENSE.md file for details on reuse and 
 * 	redistribution.
 * 	
 * 	Based on libraries from Luke Miller. 
 * 	Modified by Ted Langhorst, March 2023.
 */


#ifndef __MS_5803__
#define __MS_5803__

#include <Arduino.h>

class MS_5803 {
public:
    // Constructor for the class. 
    // The arguments are I2C adddress and oversampling resolution, 
    // valid addresses are: 0x76 and 0x77.
    // valid resolutions are: 256, 512, 1024, 2048, 4096
    MS_5803(uint8_t Version = 14, byte I2C_Address = 0x76, uint16_t Resolution = 512);
    // Initialize the sensor 
    boolean initializeMS_5803();
    // Reset the sensor
    void resetSensor();
    // Read the sensor
    void readSensor();
    //*********************************************************************
    // Additional methods to extract temperature, pressure (mbar), and the 
    // D1,D2 values after readSensor() has been called
	
    // Return temperature in degrees Celsius.
    int16_t getTemperature() const       {return (int16_t)T;}  
    // Return pressure in mbar.
    uint32_t getPressure() const          {return (uint32_t)P;}
    // Return the D1 and D2 values, mostly for troubleshooting
    unsigned long D1val() const 	{return D1;}
    unsigned long D2val() const		{return D2;}
    
private:
    int32_t P; // pressure [bar*10^-5], initially as a signed long integer for math purposes.
	int32_t T; // temperature [C*10^-2], initially as a signed long integer for math purposes.
    unsigned long D1;	// Store D1 value
    unsigned long D2;	// Store D2 value
    // Check data integrity with CRC4
    unsigned char MS_5803_CRC(unsigned int n_prom[]); 
    // Handles commands to the sensor.
    unsigned long MS_5803_ADC(char commandADC);
    // Oversampling resolution
    uint16_t _Resolution;
    // I2C Address
    byte _I2C_Address;
	// Pressure range version
	uint8_t _Version;
};

#endif 