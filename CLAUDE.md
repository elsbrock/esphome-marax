# ESPHome Mara X Display

ESP32-S3 + 3.5" touchscreen displaying Lelit Mara X espresso machine data via UART.

## Status
✅ Hardware working (display, WiFi, touchscreen)  
⏳ Next: UART parsing + UI improvements

## Key Files
- `marax-display.yaml` - Main ESPHome config
- `secrets.yaml` - WiFi credentials

## Mara X Protocol
UART data: `C1.06,116,124,093,0840,1,0\n`
- Pos 0: Mode (C/V), Pos 2: Steam temp, Pos 3: Target temp, Pos 4: HX temp
- Pos 6: Heat (0/1), Pos 7: Pump (0/1)

## Hardware Pins  
- Display: QSPI (clk=47, data=[21,48,40,39], cs=45)
- Touch: I2C (sda=4, scl=8) 
- UART: tx=43, rx=44, 9600 baud