# Custom Firmware for Ploopy Trackball

This is a custom firmware designed to modify and enhance the Ploopy Trackball.  
It is intended to be used together with the source code provided in this repository.

---

## How to Build the Firmware

### 1. Set up QMK

Set up your QMK build environment.

`qmk setup`

### 2. Obtain this repository

Clone this repository from GitHub or download and extract the ZIP archive.

### 3. About post_rules.mk

This firmware uses a custom `post_rules.mk` for the Ploopy Trackball.  
Its contents differ from the one included in the main QMK repository,  
so please overwrite the QMK version with the `post_rules.mk` provided here.

---

## Build

`qmk compile -kb ploopyco/trackball -km nyanz-lab`

---

## Flashing the Firmware

### Entering Bootloader Mode

Hold the lower‑left button while connecting the USB cable.  
The device will start in bootloader mode and appear as a UF2 drive on your PC.

### Using the QMK CLI

`qmk flash -kb ploopyco/trackball -km nyanz-lab`

### Drag‑and‑drop flashing

Drag and drop the generated `.uf2` file onto the UF2 drive that appears.

---

## Notes

* Using older versions of the official Ploopy firmware may prevent the device from entering bootloader mode.
