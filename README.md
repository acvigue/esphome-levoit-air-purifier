# Levoit Air Purifier Integration for ESPHome

## Supported:
- Levoit Core 200s (Need Confirmation)
- Core 300s (Tested)
- Core 400s (Tested, thank you @SeveranExp)

## Disassembly
For details on the teardown please read the [blog post](https://vigue.me/posts/levoit-air-purifier-esphome-conversion)
- Place upside down and remove base cover and filter to expose 8 screws (4 have washers)
- Remove all 8 screws *be careful, as these are made out of a soft metal*
- Using a pry tool slide in between tabs
- Separate base and top sleeve
- Unplug logic board

## Flash
- Copy the provided sample configuration for your model to a new ESPHome configuration, while keeping the generated passwords
- Compile and download the binary (Choose the modern format once compilation has completed)
- Solder wires to pins TXD0, RXD0, IO0, +3V3, and GND near the ESP32 on the logic board, and connect these to a USB-UART converter
- Connect IO0 to ground during power before connecting USB-UART to boot to bootloader.

### Backup Existing Firmware
```bash
esptool.py read_flash 0 ALL levoit.bin
```

### Erase Flash
```bash
esptool.py erase_flash
```

### Install New Firmware
```bash
esptool.py write_flash 0x00 esphome-firmware.bin
```

& reassemble, enjoy :)

# Contributing
All contributions are welcome! Please open an issue or a PR.

## Contributors:

Aiden (@acvigue) - Original code & Core300s support
Ryan Lang (@ryan-lang) - Core400s support
Havarius (@Havarius) - Core200s support
Haydon (@haydonryan) - Documentation & testing
