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

#include "MS5803.h"
#include <Wire.h>

#define CMD_RESET		0x1E	// ADC reset command
#define CMD_ADC_READ	0x00	// ADC read command
#define CMD_ADC_CONV	0x40	// ADC conversion command
#define CMD_ADC_D1		0x00	// ADC D1 conversion
#define CMD_ADC_D2		0x10	// ADC D2 conversion
#define CMD_ADC_256		0x00	// ADC resolution=256
#define CMD_ADC_512		0x02	// ADC resolution=512
#define CMD_ADC_1024	0x04	// ADC resolution=1024
#define CMD_ADC_2048	0x06	// ADC resolution=2048
#define CMD_ADC_4096	0x08	// ADC resolution=4096

// Create array to hold the 8 sensor calibration coefficients
static unsigned int      sensorCoeffs[8]; // unsigned 16-bit integer (0-65535)
// D1 and D2 need to be unsigned 32-bit integers (long 0-4294967295)
static uint32_t     D1 = 0;    // Store uncompensated pressure value
static uint32_t     D2 = 0;    // Store uncompensated temperature value
// These three variables are used for the conversion steps
// They should be signed 32-bit integer initially 
// i.e. signed long from -2147483648 to 2147483647
static int32_t	dT = 0;
// These values need to be signed 64 bit integers 
// (long long = int64_t)
static int64_t	Offset = 0;
static int64_t	Sensitivity  = 0;
static int64_t	T2 = 0;
static int64_t	OFF2 = 0;
static int64_t	Sens2 = 0;
// bytes to hold the results from I2C communications with the sensor
static byte HighByte;
static byte MidByte;
static byte LowByte;

// Some constants used in calculations below
const uint64_t POW_2_33 = 8589934592ULL; // 2^33 = 8589934592
const uint64_t POW_2_37 = 137438953472ULL; // 2^37 = 137438953472

//-------------------------------------------------
// Constructor
MS_5803::MS_5803(uint8_t Version, byte I2C_Address, uint16_t Resolution) {
	//MS5803 version, currently only supports 2, 5, and 14 bar.
	_Version = Version;
	
	//I2C resolution, which may have values of 0x76 or 0x77.
	_I2C_Address = I2C_Address;

	// oversampling resolution, which may have values of: 256, 512, 1024, 2048, or 4096.
	_Resolution = Resolution;
}

//-------------------------------------------------
boolean MS_5803::initializeMS_5803() {
    Wire.begin();
    // Reset the sensor during startup
    resetSensor(); 
    
    #ifdef DEBUG_SERIAL
    	// Display the constructor settings or an error message
		if (_Version == 2 || _Version == 5 || _Version == 14){
			DEBUG_SERIAL.print("MS5803 Version: ");
			DEBUG_SERIAL.print(_Version);
			DEBUG_SERIAL.println(" Bar maximum");
		} else { 
			DEBUG_SERIAL.println("*******************************************");
			DEBUG_SERIAL.println("Error: specify a valid MS5803 version");
			DEBUG_SERIAL.println("Library currently only supports 2, 5, and 14 Bar versions.");
			DEBUG_SERIAL.println("*******************************************");
		}
		if (_I2C_Address == 0x76 || _I2C_Address == 0x77){
			DEBUG_SERIAL.print("I2C Address: ");
			DEBUG_SERIAL.println(_I2C_Address);
		} else { 
			DEBUG_SERIAL.println("*******************************************");
			DEBUG_SERIAL.println("Error: specify a valid I2C address");
			DEBUG_SERIAL.println("Choices are 0x76 or 0x77");
			DEBUG_SERIAL.println("*******************************************");
		}
		if (_Resolution == 256 | _Resolution == 512 | _Resolution == 1024 | _Resolution == 2048 | _Resolution == 4096){
				DEBUG_SERIAL.print("Oversampling setting: ");
				DEBUG_SERIAL.println(_Resolution);    		
		} else {
			DEBUG_SERIAL.println("*******************************************");
			DEBUG_SERIAL.println("Error: specify a valid oversampling value");
			DEBUG_SERIAL.println("Choices are 256, 512, 1024, 2048, or 4096");			
			DEBUG_SERIAL.println("*******************************************");
		}
		//ensure the messages are sent before potentially invalid settings are used below.
		DEBUG_SERIAL.flush(); 
    #endif

    // Read sensor coefficients
    for (int i = 0; i < 8; i++ ){
    	// The PROM starts at address 0xA0
    	Wire.beginTransmission(_I2C_Address);
    	Wire.write(0xA0 + (i * 2));
    	Wire.endTransmission();
    	Wire.requestFrom(_I2C_Address, 2);
    	while(Wire.available()) {
    		HighByte = Wire.read();
    		LowByte = Wire.read();
    	}
    	sensorCoeffs[i] = (((unsigned int)HighByte << 8) + LowByte);
    	#ifdef DEBUG_SERIAL
			// Print out coefficients 
			DEBUG_SERIAL.print("C");
			DEBUG_SERIAL.print(i);
			DEBUG_SERIAL.print(" = ");
			DEBUG_SERIAL.println(sensorCoeffs[i]);
			DEBUG_SERIAL.flush(); 
    	#endif
    }
    // The last 4 bits of the 7th coefficient form a CRC error checking code.
    unsigned char p_crc = sensorCoeffs[7];
	p_crc &= 0b00001111;
    // Use a function to calculate the CRC value
    unsigned char n_crc = MS_5803_CRC(sensorCoeffs); 
    
    #ifdef DEBUG_SERIAL
		DEBUG_SERIAL.print("p_crc: ");
		DEBUG_SERIAL.println(p_crc);
		DEBUG_SERIAL.print("n_crc: ");
		DEBUG_SERIAL.println(n_crc);
    #endif
	
    // check that coefficients are not all 0. 
    // without this check, CRC will pass despite unresponsive sensor.
    bool empty_coeffs = true;
    for (int i = 0; i<8; i++){
		if (sensorCoeffs[i] !=0){
			empty_coeffs = false;
			break;
		}
    }

    if (p_crc != n_crc || empty_coeffs) {
        return false;
    }
		
    // Otherwise, return true when everything checks out OK. 
    return true;
}

//------------------------------------------------------------------
void MS_5803::readSensor() {
	// Choose from CMD_ADC_256, 512, 1024, 2048, 4096 for mbar resolutions
	// of 1, 0.6, 0.4, 0.3, 0.2 respectively. Higher resolutions take longer
	// to read.
	if (_Resolution == 256){
		D1 = MS_5803_ADC(CMD_ADC_D1 + CMD_ADC_256); // read raw pressure
		D2 = MS_5803_ADC(CMD_ADC_D2 + CMD_ADC_256); // read raw temperature	
	} else if (_Resolution == 512) {
		D1 = MS_5803_ADC(CMD_ADC_D1 + CMD_ADC_512); // read raw pressure
		D2 = MS_5803_ADC(CMD_ADC_D2 + CMD_ADC_512); // read raw temperature		
	} else if (_Resolution == 1024) {
		D1 = MS_5803_ADC(CMD_ADC_D1 + CMD_ADC_1024); // read raw pressure
		D2 = MS_5803_ADC(CMD_ADC_D2 + CMD_ADC_1024); // read raw temperature
	} else if (_Resolution == 2048) {
		D1 = MS_5803_ADC(CMD_ADC_D1 + CMD_ADC_2048); // read raw pressure
		D2 = MS_5803_ADC(CMD_ADC_D2 + CMD_ADC_2048); // read raw temperature
	} else if (_Resolution == 4096) {
		D1 = MS_5803_ADC(CMD_ADC_D1 + CMD_ADC_4096); // read raw pressure
		D2 = MS_5803_ADC(CMD_ADC_D2 + CMD_ADC_4096); // read raw temperature
	}
    // Calculate 1st order temperature, dT is a long integer
	// D2 is originally cast as an uint32_t, but can fit in a int32_t, so we'll
	// cast both parts of the equation below as signed values so that we can
	// get a negative answer if needed
    dT = (int32_t)D2 - ( (int32_t)sensorCoeffs[5] * 256 );
    // Use integer division to calculate T. It is necessary to cast
    // one of the operands as a signed 64-bit integer (int64_t) so there's no 
    // rollover issues in the numerator.
    T = 2000 + ((int64_t)dT * sensorCoeffs[6]) / 8388608LL;
    // Recast T as a signed 32-bit integer
    T = (int32_t)T;

    
    // All operations from here down are done as integer math until we make
    // the final calculation of pressure in mbar. 
    
    
    // Do 2nd order temperature compensation (see pg 9 of MS5803 data sheet)
    // I have tried to insert the fixed values wherever possible 
    // (i.e. 2^31 is hard coded as 2147483648).
    if (T < 2000) { // If temperature is below 20.0C
		switch(_Version){
			case 14:
				T2 = 3 * ((int64_t)dT * dT) / POW_2_33 ;
				T2 = (int32_t)T2; // recast as signed 32bit integer
				OFF2 = 3 * ((T-2000) * (T-2000)) / 2 ;
				Sens2 = 5 * ((T-2000) * (T-2000)) / 8 ; 	
				break;
			case 5:
				T2 = 3 * ((int64_t)dT * dT)  / POW_2_33 ;
				T2 = (int32_t)T2; // recast as signed 32bit integer
				OFF2 = 3 * ((T-2000) * (T-2000)) / 8 ;
				Sens2 = 7 * ((T-2000) * (T-2000)) / 8 ;
				break;
			case 2:
				T2 = ((int64_t)dT * dT) / 2147483648LL ; // 2^31 = 2147483648
				T2 = (int32_t)T2; // recast as signed 32bit integer
				OFF2 = (61 * ((T-2000) * (T-2000))) / 16 ;
				Sens2 = 2 * ((T-2000) * (T-2000)) ;
				break;
		}
    } else { // if T is > 2000 (20.0C)
		switch(_Version){
			case 14:
				T2 = 7 * ((uint64_t)dT * dT) / POW_2_37;
				T2 = (int32_t)T2; // recast as signed 32bit integer
				OFF2 = 1 * ((T-2000) * (T-2000)) / 16;
				Sens2 = 0;
				break;
			case 5:
				T2 = 0;
				OFF2 = 0;
				Sens2 = 0;
				break;
			case 2:
				T2 = 0;
				OFF2 = 0;
				Sens2 = 0;
				break;
		}
    }
    
    // Additional compensation for very low temperatures (< -15C)
    if (T < -1500) {
		switch(_Version){
			case 14:
				OFF2 = OFF2 + 7 * ((T+1500)*(T+1500));
				Sens2 = Sens2 + 4 * ((T+1500)*(T+1500));
				break;
			case 5: 
				// No additional correction for 5 bar version.
				break;
			case 2:
				OFF2 = OFF2 + 20 * ((T+1500)*(T+1500));
				Sens2 = Sens2 + 12 * ((T+1500)*(T+1500));
				break;
		}
    }
    
    // Calculate initial Offset and Sensitivity
    // Notice lots of casts to int64_t to ensure that the 
    // multiplication operations don't overflow the original 16 bit and 32 bit
    // integers
	switch(_Version){
		case 14:
			Offset = (int64_t)sensorCoeffs[2] * 65536 + (sensorCoeffs[4] * (int64_t)dT) / 128;
			Sensitivity = (int64_t)sensorCoeffs[1] * 32768 + (sensorCoeffs[3] * (int64_t)dT) / 256;	
			break;
		case 5:
			Offset = (int64_t)sensorCoeffs[2] * 262144 + (sensorCoeffs[4] * (int64_t)dT) / 32;
			Sensitivity = (int64_t)sensorCoeffs[1] * 131072 + (sensorCoeffs[3] * (int64_t)dT) / 128;
			break;
		case 2:
			Offset = (int64_t)sensorCoeffs[2] * 131072 + (sensorCoeffs[4] * (int64_t)dT) / 64;
			Sensitivity = (int64_t)sensorCoeffs[1] * 65536 + (sensorCoeffs[3] * (int64_t)dT) / 128;
			break;
	}
  
    // Adjust T, Offset, Sensitivity values based on the 2nd order 
    // temperature correction above.
    T = T - T2; // both should be int32_t
    Offset = Offset - OFF2; // both should be int64_t
    Sensitivity = Sensitivity - Sens2; // both should be int64_t
 
	// Convert final values to human-readable floats.
	switch(_Version){
		case 14:
			P = ((D1 * Sensitivity) / 2097152 - Offset) / 32768 * 10;
			break;
		case 5: 
			P = ((D1 * Sensitivity) / 2097152 - Offset) / 32768;
			break;
		case 2:
			P = ((D1 * Sensitivity) / 2097152 - Offset) / 32768;
			break;	
	}
}

//------------------------------------------------------------------
// Function to check the CRC value provided by the sensor against the 
// calculated CRC value from the rest of the coefficients. 
// Based on code from Measurement Specialties application note AN520
// http://www.meas-spec.com/downloads/C-Code_Example_for_MS56xx,_MS57xx_%28except_analog_sensor%29_and_MS58xx_Series_Pressure_Sensors.pdf
unsigned char MS_5803::MS_5803_CRC(unsigned int n_prom[]) {
    int cnt;				// simple counter
    unsigned int n_rem;		// crc reminder
    unsigned int crc_read;	// original value of the CRC
    unsigned char  n_bit;
    n_rem = 0x00;
    crc_read = sensorCoeffs[7];		// save read CRC
    sensorCoeffs[7] = (0xFF00 & (sensorCoeffs[7])); // CRC byte replaced with 0
    for (cnt = 0; cnt < 16; cnt++)
    { // choose LSB or MSB
        if (cnt%2 == 1) {
        	n_rem ^= (unsigned short)((sensorCoeffs[cnt>>1]) & 0x00FF);
        }
        else {
        	n_rem ^= (unsigned short)(sensorCoeffs[cnt>>1] >> 8);
        }
        for (n_bit = 8; n_bit > 0; n_bit--)
        {
            if (n_rem & (0x8000))
            {
                n_rem = (n_rem << 1) ^ 0x3000;
            }
            else {
                n_rem = (n_rem << 1);
            }
        }
    }
    n_rem = (0x000F & (n_rem >> 12));// // final 4-bit reminder is CRC code
    sensorCoeffs[7] = crc_read; // restore the crc_read to its original place
    // Return n_rem so it can be compared to the sensor's CRC value
    return (n_rem ^ 0x00); 
}

//-----------------------------------------------------------------
// Send commands and read the temperature and pressure from the sensor
unsigned long MS_5803::MS_5803_ADC(char commandADC) {
	// D1 and D2 will come back as 24-bit values, and so they must be stored in 
	// a long integer on 8-bit Arduinos.
    long result = 0;
    // Send the command to do the ADC conversion on the chip
	Wire.beginTransmission(_I2C_Address);
    Wire.write(CMD_ADC_CONV + commandADC);
    Wire.endTransmission();
    // Wait a specified period of time for the ADC conversion to happen
    // See table on page 1 of the MS5803 data sheet showing response times of
    // 0.5, 1.1, 2.1, 4.1, 8.22 ms for each accuracy level. 
    switch (commandADC & 0x0F) 
    {
        case CMD_ADC_256 :
            delay(1); // 1 ms
            break;
        case CMD_ADC_512 :
            delay(3); // 3 ms
            break;
        case CMD_ADC_1024:
            delay(4);
            break;
        case CMD_ADC_2048:
            delay(6);
            break;
        case CMD_ADC_4096:
            delay(10);
            break;
    }
    // Now send the read command to the MS5803 
    Wire.beginTransmission(_I2C_Address);
    Wire.write((byte)CMD_ADC_READ); // added cast
    Wire.endTransmission();
    // Then request the results. This should be a 24-bit result (3 bytes)
    Wire.requestFrom(_I2C_Address, 3);
    while(Wire.available()) {
    	HighByte = Wire.read();
    	MidByte = Wire.read();
    	LowByte = Wire.read();
    }
    // Combine the bytes into one integer
    result = ((long)HighByte << 16) + ((long)MidByte << 8) + (long)LowByte;
    return result;
}

//----------------------------------------------------------------
// Sends a power on reset command to the sensor.
void MS_5803::resetSensor() {
    	Wire.beginTransmission(_I2C_Address);
        Wire.write(CMD_RESET);
        Wire.endTransmission();
    	delay(10);
}
