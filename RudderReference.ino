#include "NMEA2000_CAN.h"
#include "N2kMessages.h"
#include <EEPROM.h>
#include <ADC_Module.h>
#include <ADC.h>

#define EEPROM_ADDR_CONFIG 0 // starting eeprom address for config params (Teensy 2K EEPROM) 
#define STDBYPIN 14 // MCP2562 CAN tranciver standby
#define ADCRESOLUTION 16
#define DEG2RAD 0.01745329252
#define ADCNUM 1 // ADC 1 seems slightly less noisy
const uint8_t ADCPIN = PIN_A2; // ADC0 or 1
double scale = 0.00950; // volts per degree
double VREF = 3.307;	// measured ADC Reference voltage
double offsetVoltage;	// center point voltage offset
double position;		//angle in degrees
uint16_t adcCount = 0; // ADC value in counts
double volts = 0; // measured ADC voltage
double gain = 1.000;  // sensor gain  compensate for potentiometer accuracy 5K 5%
ADC* adc = new ADC();
float average = 0;
const int NUM_AVGS = 100;

// VREF_OUT test function
float GetVREF() {
	average = 0;
	for (int i = 0; i<NUM_AVGS; i++) {
		average += 1.195 / adc->analogRead(ADC_INTERNAL_SOURCE::VREF_OUT, ADCNUM)*(adc->getMaxValue(ADCNUM));
	}
	return average / NUM_AVGS;
}

// VREFH test function
float GetVREFH() {
	average = 0;
	for (int i = 0; i<NUM_AVGS; i++) {
		average += VREF / adc->analogRead(ADC_INTERNAL_SOURCE::VREFH, ADCNUM)*(adc->getMaxValue(ADCNUM));
	}
	return average / NUM_AVGS;
}

// EEPROM configuration structure
#define MAGIC 12345 // EPROM struct version check, change this whenever tConfig structure changes
struct tConfig {
	uint16_t Magic; //test if eeprom initialized
	double Zero;
	double Gain;
	uint8_t deviceInstance;
};

// EEPROM contents
tConfig config;

// Default EEPROM contents
const tConfig defConfig PROGMEM = {
	MAGIC,
	VREF/2, // zero
	1,  // unity gain
	50	// deviceInstance
};

//*****************************************************************************
tN2kMsg N2kMsg; // N2K message constructor
const unsigned long TransmitMessages[] PROGMEM = { 127245L, 0 }; //Rudder Angle																						   // 	Received Messages			

void setup() {
	pinMode(LED_BUILTIN, OUTPUT); //enable LED
	pinMode(STDBYPIN, OUTPUT);  // take CAN chip out of standby
	digitalWrite(STDBYPIN, LOW);
	pinMode(ADCPIN, INPUT);
	Serial.begin(115200);
	Blink(10, 250); //delay for 1 sec and flash LED

	adc->setReference(ADC_REFERENCE::REF_3V3, ADCNUM);
	adc->setAveraging(32, ADCNUM);
	adc->setResolution(ADCRESOLUTION, ADCNUM);
	adc->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED, ADCNUM);
	adc->setSamplingSpeed(ADC_SAMPLING_SPEED::VERY_LOW_SPEED, ADCNUM);
	adc->startContinuous(ADCPIN, ADCNUM);
	delay(100);
	Serial.println(F("STARTING"));
	ReadConfig();
	if (config.Magic != MAGIC) {
		InitializeEEPROM();
		Serial.println(F("No stored calibration..\n"));
		delay(5000);
	}
	else {
		Serial.println(F("Loading stored calibration..\n"));
		PrintConfig();
		gain = config.Gain;
		offsetVoltage = config.Zero;
	}
	
	// report the supply voltages
	Serial.print(F("3.3V ADC reference : "));
	Serial.print(GetVREFH(),4);
	Serial.println("V");
	Serial.print("VREF OUT voltage: ");
	Serial.print(GetVREF(), 4);
	Serial.println("V");	delay(2000);

	// Setup NMEA2000
	NMEA2000.SetInstallationDescription1("Mr Bubble Rudder Reference");
	NMEA2000.SetProductInformation("20180317", // Manufacturer's Model serial code
		669, // Manufacturer's product code
		"Rudder Reference",  // Manufacturer's Model ID
		"1.0.0.0",  // Manufacturer's Software version code
		"1.0.0.0", // Manufacturer's Model version
		2,	// load equivalency *50ma
		0xffff, // NMEA 2000 version - use default
		0xff // Sertification level - use default
	);
	// Set device information
	NMEA2000.SetDeviceInformation(20180317, // Unique number. Use e.g. Serial number.
		155, // Device function=Environment See codes on http://www.nmea.org/Assets/20120726%20nmea%202000%20class%20%26%20function%20codes%20v%202.00.pdf
		40, // Device class=Sensor Communication Interface. See codes on http://www.nmea.org/Assets/20120726%20nmea%202000%20class%20%26%20function%20codes%20v%202.00.pdf
		2040, // Just choosen free from code list on http://www.nmea.org/Assets/20121020%20nmea%202000%20registration%20list.pdf                               
		4 // marine industry
	);
	NMEA2000.SetMode(tNMEA2000::N2km_NodeOnly, config.deviceInstance);
	NMEA2000.EnableForward(false);
	NMEA2000.ExtendTransmitMessages(TransmitMessages);
	NMEA2000.SetN2kCANMsgBufSize(5);
	NMEA2000.Open();
	PrintHelp();
}

// loop controls
uint32_t delt_t = 0; // 250 ms loop
uint32_t count = 0; // 250 ms loop
long slowloop = 0; // 1s loop

// USB serial commands
bool SDEBUG = false; // toggle debug print
bool SDATA = false; // toggle data print
int GAIN; // gain switch

void loop() {
	char command = getCommand();
	switch (command) {
	case '+':
		GAIN = 1;
		break;
	case '-':
		GAIN = -1;
		break;
	case 'd':
		SDEBUG = !SDEBUG;
		break;
	case 'w':
		config.Gain = gain;
		UpdateConfig();
		break;
	case 'z': 
		offsetVoltage = volts;
		config.Zero = offsetVoltage;
		UpdateConfig();
		break;
	default:
		break;
	}

	if (adc->isComplete(ADCNUM)) {
		adcCount = (uint16_t)adc->analogReadContinuous(ADCNUM);
	}

	// 250ms 
	delt_t = millis() - count;
	if (delt_t > 250) { // fast update once per 250ms independent of read rate
		count = millis();
		digitalWrite(LED_BUILTIN, LOW);

		if (GAIN != 0) {
			SDATA = true;
			gain += (gain / 500)*GAIN;
			Serial.print("GAIN: ");
			Serial.println(gain);
			GAIN = 0;
		}

		volts = adcCount * VREF / (double)adc->getMaxValue(ADCNUM);
		position = gain * (volts - offsetVoltage)/scale;
		
		// debug
		if (SDEBUG) { printDebug(); }
		if (SDATA) { print(); }

		// send rudder angle 
		SetN2kRudder(N2kMsg, position*DEG2RAD);
		NMEA2000.SendMsg(N2kMsg);

		slowloop++;
	}
	// 1000ms
	if (slowloop > 3) { slowloop = 0; SlowLoop(); }
	NMEA2000.ParseMessages();
}

// slow message loop
void SlowLoop() {
	// Slow loop N2K message processing
	// check device instance for instructions from MFD
	tNMEA2000::tDeviceInformation DeviceInformation = NMEA2000.GetDeviceInformation();
	uint8_t DeviceInstance = DeviceInformation.GetDeviceInstance();
	if (DeviceInstance != config.deviceInstance) {
		// set instance to 200 to set midship zero angle point.
		if (DeviceInstance == 200) {
			Serial.println("\nSetting zero point");
			offsetVoltage = volts;
			config.Zero = offsetVoltage;
			UpdateConfig();
			DeviceInstance = config.deviceInstance;
			DeviceInformation.SetDeviceInstance(DeviceInstance);
			Blink(20, 150);
		}
		else {
			Serial.print("\nInstance changed to: "); Serial.print(DeviceInstance);
			config.deviceInstance = DeviceInstance;
			UpdateConfig();
		}
	}
	digitalWrite(LED_BUILTIN, HIGH);
}

// read serial input
char getCommand()
{
	char c = '\0';
	if (Serial.available())
	{
		c = Serial.read();
	}
	return c;
}

//*****************************************************************************
// LED blinker count flashes in duration ms
void Blink(int count, unsigned long duration) {
	unsigned long d = duration / count;
	for (int counter = 0; counter < count; counter++) {
		digitalWrite(LED_BUILTIN, HIGH);
		delay(d / 2);
		digitalWrite(LED_BUILTIN, LOW);
		delay(d / 2);
	}
}

// EEPROM *****************************************************************************
//Load From EEPROM 
void ReadConfig() {
	EEPROM.get(EEPROM_ADDR_CONFIG, config);
}

//Write to EEPROM - Teensy non-volatile area size is 2048 bytes  100,000 cycles
void UpdateConfig() {
	Blink(5, 2000);
	Serial.println("Updating config");
	config.Magic = MAGIC;
	EEPROM.put(EEPROM_ADDR_CONFIG, config);
}

// Load default config into EEPROM
void InitializeEEPROM() {
	Serial.println("Initialize EEPROM");
	config = defConfig;
	UpdateConfig();
}

void print(){
	Serial.print("Rudder Angle: ");
	Serial.print(position);
	Serial.print(" deg ");
	Serial.print(position*DEG2RAD,4);
	Serial.println(" rad");
}

void printDebug() {
	Serial.print("ADC count: ");
	Serial.print(adcCount);
	Serial.print(" Abs: ");
	Serial.print(volts,4);
	Serial.print("V Rel: ");
	Serial.print(volts - offsetVoltage,4);
	Serial.print("V Offset: ");
	Serial.print(offsetVoltage, 4);
	Serial.print(" Gain: ");
	Serial.print(gain,4);
	Serial.print(" ");
}

void PrintConfig() {
	Serial.print("Center zero offset: ");
	Serial.print(offsetVoltage);
	Serial.print("V\n");
	Serial.print("Gain: ");
	Serial.print(gain, 6);
	Serial.println("\n");
}

void PrintHelp()
{
	Serial.println(F("Mr Bubble Rudder Reference"));
	Serial.println(F("Commands: \"p\"=show data, \"d\"=show ADC voltages, \"z\"=save current position as zero reference, \"+\"=ingrease gain,\"-\"=decrease gain, \"w\"=save gain setting to EEPROM"));
	Serial.println(F("Alignment: Center the wheel and press 'z' to align the rudder reference, turn to 90 degrees and use the + and - keys to adjust gain"));
}
