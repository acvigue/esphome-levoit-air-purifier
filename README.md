# Levoit Air Purifier Integration for ESPHome

## Supported Devices
* Levoit Core 200s (Confirmed)
* Core 300s (Confirmed)
* Core 400s (Confirmed, thank you @SeveranExp)

## Disassembly
For details on the teardown, please read [this blog post](https://vigue.me/posts/levoit-air-purifier-esphome-conversion).
* Place device upside down and remove base cover and filter.
  * On the Core 300s, 8 screws will be exposed, 4 black screws and 4 with washers attached.
  * On the Core 200s, there will only be 4 screws with washers attached.
* Remove all exposed screws. (*Be careful, these are made out of a soft metal*.)
* Using a pry tool, press into the tabs and gently separate the two pieces.
* Completely separate the base (holds the filter) and the top (holds the fan).
* Slide the entire fan out of the top.
* Unplug logic board from the fan. There's a clip, on the plug, that must be pressed in order for the plug to be removed.

## Flash
* Copy the provided sample configuration for your model to a new ESPHome configuration, while keeping the generated passwords.
* Compile and download the binary (Choose the modern format once compilation has completed).
  * If using esphome on the command line, run `esp compile levoit300s.yaml`. The modern format binary will be located in `.esphome/build/levoit300s/.pioenvs/levoit300s/firmware.factory.bin`.
* Solder wires to pins TXD0, RXD0, IO0, +3V3, and GND near the ESP32 on the logic board, and connect these to a USB UART converter. On some boards, if these are through holes, soldering may not be necessary.
  * The Core 200s has a spot for +5V and no IO0.
* Connect IO0 to ground while applying power to boot into the bootloader.
  * If no IO0 is available, use the GPIO0 pin directly on the ESP32 (right side, very bottom).

### Backup Existing Firmware
```bash
esptool read-flash 0 ALL levoit300s-stock_firmware-$(date +"%Y%m%d").bin
```

### Erase Flash
```bash
esptool erase-flash
```

### Install New Firmware
```bash
esptool write-flash 0x00 esphome-firmware.bin
```

Disconnect and reconnect the USB UART to make sure the ESP32 restarts. Once powered, check to make sure the ESP32 connects to your wireless network.

After confirming, reassemble, and enjoy :)

## Filter Life
The `filter_life` and `filter_runtime` sensors track filter usage and drive the replace-filter indicator. Tracking starts at 100% / 0 h unless `filter_initial_runtime_minutes` and `filter_initial_dust` are set; these are only used when no filter state has been saved yet.

If you want to carry over the old values, read them **before erasing** the flash (tested on a Core 300S,  firmware 1.1.09):
```bash
esptool read-flash 0x310000 0x20000 usercfg.bin
python $IDF_PATH/components/nvs_flash/nvs_partition_tool/nvs_tool.py -d blobs usercfg.bin
```
In the 20-byte `airfilter_info` blob, bytes 0-3 are the runtime in minutes and bytes 8-11 the dust, both little-endian (e.g. `a2 48 02 00` = 149666). Remove these options once the values have been saved.

# Contributing
All contributions are welcome! Please open an issue or a PR.

## Contributors

* Aiden (@acvigue) - Original code & Core300s support
* Ryan Lang (@ryan-lang) - Core400s support
* Havarius (@Havarius) - Core200s support
* Haydon (@haydonryan) - Documentation & testing
