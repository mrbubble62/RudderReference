# Rudder Reference
Low cost NMEA 2000 compatible rudder reference

## PGN's sent 
127245 Rudder angle in radians every 250ms

## Initial calibration via USB serial
Center the sensor and press 'z' to set zero angle, turn 90 degrees and use the + and - keys to adjust gain

## Zero setting in vessel
Center the rudder and from an MFD change the device instance to 200.
The current setting will be stored in EEPROM as the zero point.
The device will return to the prevoiusly set N2K device instance, this may confuse the MFD so power cycle the system.

### Tested Hardware
- Teensy 3.2
- MCP2562 CAN Transceiver
- LM7805 5V Linear regulator
- 0.25A Poly fuse
- 5K ohm Linear 0.1% WDD35D4-5K Angle Sensors

