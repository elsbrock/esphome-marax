# ESPHome Mara X Display

A professional temperature monitoring display for the Lelit Mara X espresso machine, built with ESP32-S3 and 3.5" touchscreen displays.

## Features

- **Real-time temperature monitoring** with live graphs
- **Professional LVGL interface** with smooth animations
- **Multi-series temperature charts** (Steam, HX, Target temperatures)
- **5-minute rolling window** with 5-second data resolution
- **Smart connectivity** - seamless switching between test and live data
- **Touch-friendly interface** optimized for espresso workflow
- **Material Design icons** for intuitive operation

## Supported Devices

| Display Model | Status | Configuration File |
|---------------|--------|--------------------|
| jc3248w535 | ✅ Tested | `jc3248w535-marax.yaml` |

### Hardware Requirements

- ESP32-S3 development board (tested with ESP32-S3-DevKitC-1)
- 3.5" touchscreen display (480x320 QSPI) - see supported models above
- Touch controller: AXS15231 
- UART connection to Lelit Mara X machine

### Pin Configuration

| Function | ESP32-S3 Pin |
|----------|--------------|
| Display CLK | GPIO47 |
| Display Data | GPIO21, 48, 40, 39 |
| Display CS | GPIO45 |
| Display Backlight | GPIO1 |
| Touch SDA | GPIO4 |
| Touch SCL | GPIO8 |
| UART TX | GPIO43 |
| UART RX | GPIO44 |

## Quick Start

### Option 1: Pre-built Firmware (Recommended)

1. **Download firmware** from the [latest release](https://github.com/elsbrock/esphome-marax/releases)
   - Download `jc3248w535-marax-esp32s3.factory.bin` for complete installation

2. **Flash using ESPHome Web** (Chrome/Edge required):
   - Connect ESP32-S3 via USB to your computer
   - Open [ESPHome Web](https://web.esphome.io/) in Chrome or Edge
   - Click "Connect" and select your ESP32 device from the popup
   - Click "Install" and select the downloaded `.factory.bin` file
   - **Optional**: Change the device name from `jc3248w535-marax` to something friendlier like `Mara X Display`
   - Wait for flashing to complete

3. **Configure WiFi** via the captive portal that appears on first boot:
   - Look for WiFi network named `Marax-Display Fallback Hotspot`
   - Connect using password: `12345678`
   - Follow the setup wizard to configure your home WiFi

### Future Updates (OTA)

Once your device is connected to WiFi, you can update firmware over-the-air:

- **ESPHome Dashboard**: Add the device and click "Install Wirelessly"
- **Web Updates**: Use the `.ota.bin` file for wireless updates via ESPHome Web
- **Home Assistant**: Updates can be managed through the ESPHome integration

### Option 2: Build from Source

1. **Clone the repository:**
   ```bash
   git clone https://github.com/elsbrock/esphome-marax.git
   cd esphome-marax
   ```

2. **Configure secrets:**
   ```bash
   cp secrets.yaml.example secrets.yaml
   # Edit secrets.yaml with your WiFi credentials and API keys
   ```

3. **Flash to ESP32:**
   ```bash
   esphome run jc3248w535-marax.yaml
   ```

## Configuration Structure

The project uses a modular configuration approach for maintainability:

```
jc3248w535-marax.yaml      # Main hardware configuration
config/
├── display_ui.yaml         # LVGL interface components  
├── fonts.yaml             # Font definitions and icons
├── sensors.yaml           # Temperature sensor setup
└── uart_parser.yaml       # Mara X protocol parsing
includes/
└── chart_helpers.h        # Temperature chart implementation
```

## Mara X Protocol

The display communicates with the Mara X via UART at 9600 baud using **inverted serial logic**. The protocol format is:

```
C1.06,116,124,093,0840,1,0\n
```

Where:
- `C1.06` - Firmware version
- `116` - Steam temperature (°C)
- `124` - Target temperature (°C)  
- `093` - HX temperature (°C)
- `0840` - Timer/timestamp
- `1` - Heat element status (0/1)
- `0` - Pump status (0/1)

## Display Interface

### Main Screen Layout

- **Left Panel**: Current temperature readings, timer, version info
- **Center**: Real-time temperature chart with 5-minute history
- **Bottom**: System status indicators (Heat, Pump, UART connection)

### Temperature Chart

- **Red line**: Steam temperature
- **Blue line**: HX/Brew temperature  
- **Green line**: Target temperature
- **Grid**: Subtle dotted lines for easy reading
- **Axes**: Temperature scale (°C) and time scale (-5m to 0m)

## Development

### Building from Source

Requirements:
- [ESPHome](https://esphome.io/) 2023.12.0 or later
- Platform: ESP32-S3 with ESP-IDF framework

### Testing

The system includes intelligent test data generation:
- Displays animated sine waves when no UART connection is detected
- Automatically switches to real data when Mara X is connected
- No manual switching required

### Architecture

- **Hardware abstraction**: Clean separation between display and UART logic
- **Modular design**: Each component in its own configuration file
- **LVGL integration**: Professional graphics with hardware acceleration
- **Real-time updates**: Efficient data flow from UART to display

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## Troubleshooting

### No UART Data
- Verify UART connections (TX/RX pins)
- Check that `inverted: true` is set on UART pins (Mara X uses inverted logic)
- Monitor logs for connection status

### Display Issues
- Ensure PSRAM is enabled and configured for octal mode
- Verify SPI pin connections for display
- Check touch controller I2C connections

### Performance
- LVGL charts are optimized for smooth updates
- 5-second data intervals balance responsiveness with memory usage
- Auto-scaling Y-axis adapts to temperature ranges

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- [ESPHome](https://esphome.io/) team for the excellent framework
- [LVGL](https://lvgl.io/) for the graphics library
- Lelit for creating an espresso machine worth monitoring
- Coffee enthusiasts everywhere who appreciate good data visualization

---

*Built with precision for the perfect espresso shot.*
