# wp81BmsTool

A small command-line tool for Windows Phone 8.1 (ARM32) that sends an IOCTL to the Qualcomm PMIC Battery Monitoring System driver (`\\.\QCOMPMICBMS`) and prints the result.

It is meant for exploring and debugging the BMS driver: reading the battery current, voltage and state of charge, or sending the `SET_*` requests with your own input values.

## Usage

```
wp81BmsTool [-o outBytes] [-i inBytes] [-d devicePath] <ioctl> [input dword ...]
wp81BmsTool list
```

| Argument | Description |
|---|---|
| `<ioctl>` | A numeric code (`0x80180FAC`) or a name (`GET_PERCENT_CHARGE`, `IOCTL_BMS_GET_PERCENT_CHARGE` or the alias `percent`). Names are case-insensitive. |
| `input` | 32-bit input values, decimal or `0x` hex. Negative values are allowed. |
| `-o n` | Output buffer size in bytes. The default is the size the driver requires (4 if the IOCTL is unknown). |
| `-i n` | Input buffer size in bytes. The default is the size the driver requires; missing input values are sent as 0. |
| `-d path` | Device path. The default is `\\.\QCOMPMICBMS`. |
| `list` | Prints the known IOCTLs with their required buffer sizes. |

Unknown IOCTL codes can be sent too; the output is then shown as a raw hex dump and as dwords.

The driver checks both buffer lengths for an **exact** match against its own descriptor table and fails any other size with `ERROR_INVALID_PARAMETER` before the request is handled. The tool therefore uses the required sizes by default, and prints a note when `-i` or `-o` sets a different size.

The driver's `BytesReturned` value is not reliable (for example, `GET_INTERNAL_CALC` writes 12 bytes but reports 4). When a known IOCTL succeeds, the tool shows the whole output buffer instead of only the reported bytes.

### Examples

```
wp81BmsTool percent
wp81BmsTool 0x80180FA4
wp81BmsTool calc
wp81BmsTool SET_SYSTEM_INFO 51139 25000 893
```

### Sample output

```
IOCTL    : 0x80180FAC IOCTL_BMS_GET_PERCENT_CHARGE
  DeviceType 0x8018, Function 1003, Method 0, Access 0
Input    : 0 bytes
Output   : 4 bytes buffer
Result   : SUCCESS
Returned : 4 bytes
Output data:
  0000: 34 03 00 00                                      4...
As dwords:     hex    unsigned       signed
  [ 0] 0x00000334         820          820
GET_PERCENT_CHARGE = 820 (0x00000334)
Note: raw internal fuel-gauge SOC, in per-mille (82.0 %);
      the phone's screen shows the OS-remapped display SOC, which differs
```

## Known IOCTLs

All codes use device type `0x8018`, functions 1000-1008, `METHOD_BUFFERED` and `FILE_ANY_ACCESS`. The In and Out columns are the exact buffer sizes the driver requires.

| Code | Name | Alias | In | Out | Inputs | Notes |
|---|---|---|---|---|---|---|
| `0x80180FA0` | `GET_BATTERY_CHARGING_PROFILE` | `profile` | 4 | 16 | ignored | **Misnamed.** Reconfigures the BMS hardware (clear, wait 70 ms, apply mode 11,2,2,6) and returns 0. It returns no charging-profile data. Do not poll it. |
| `0x80180FA4` | `GET_BATTERY_CURRENT` | `current` | 0 | 4 | none | Calibrated battery current in mA, signed. Negative means discharging. |
| `0x80180FA8` | `GET_BATTERY_VOLTAGE` | `voltage` | 0 | 4 | none | VBAT at the ADC pin in mV, before the board divider. Multiply by the divider (about 3x) to get the battery voltage, for example 1358 → about 4.07 V. |
| `0x80180FAC` | `GET_PERCENT_CHARGE` | `percent` | 0 | 4 | none | Raw internal fuel-gauge state of charge in per-mille (820 = 82.0 %). The phone's screen shows a remapped display value, for example 91 % for a raw 82.0 %. |
| `0x80180FB0` | `SET_CHARGING_STATE` | `charging` | 4 | 0 | `<state>` (0, 1 or 2) | Forwards the charging state to the PMIC. The buffer sizes are inferred, not read from the driver. |
| `0x80180FB4` | `SET_SYSTEM_INFO` | `sysinfo` | 12 | 4 | `<timestamp> <temperature> <batteryId>` | Temperature in thousandths of °C (25000 = 25 °C); it feeds the SOC curves. `batteryId` is the battery ID-resistor ADC reading (about 893), not a serial number. |
| `0x80180FB8` | `FORCE_OCV` | `ocv` | 0 | 0 | none | Does nothing in this driver build: it returns success without forcing an OCV measurement. |
| `0x80180FBC` | `SET_XOADC_CAL_VAL` | `xoadc` | 8 | 0 | `<rawPointA> <rawPointB>` | Raw XOADC codes of the 0.625 V and 1.25 V reference voltages, used for the two-point ADC calibration. |
| `0x80180FC0` | `GET_INTERNAL_CALC` | `calc` | 0 | 12 | none | Derated full-charge capacity, remaining-charge reference and remaining-charge headroom. |

> [!WARNING]
> `GET_BATTERY_CHARGING_PROFILE` and the `SET_*` requests change the state of the driver or the PMIC. The tool prints a warning before sending them. Use them with care on a real device.
>
> The phone's battery stack (BATTC and NokiaEnergyDriver) sends these IOCTLs itself, so a value you set can be overwritten shortly afterwards.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | The IOCTL succeeded (or `list` / help was printed). |
| 1 | Invalid command line. |
| 2 | The device could not be opened. |
| 3 | `DeviceIoControl` failed. |

Common errors are printed with an explanation, for example `ERROR_FILE_NOT_FOUND` when the driver is not loaded, `ERROR_INVALID_FUNCTION` when the driver rejects the IOCTL, or `ERROR_INVALID_PARAMETER` when a buffer size does not match the size the driver requires.

## Building

The tool is cross-compiled for ARM32 with clang-cl against the Windows Phone 8.1 SDK. It links only against `mincore.lib`.

### Requirements

- [CMake](https://cmake.org/) 3.20 or later
- [Ninja](https://ninja-build.org/)
- [LLVM](https://llvm.org/) with `clang-cl` and `lld-link`, installed in `C:\Program Files\LLVM`
- Windows Phone 8.1 SDK (`C:\Program Files (x86)\Windows Phone Kits\8.1`)
- Visual Studio 2012 Windows Phone SDK (`C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\WPSDK`)

If your tools are installed elsewhere, update the paths in [CMakeLists.txt](wp81BmsTool/CMakeLists.txt) and [CMakePresets.json](wp81BmsTool/CMakePresets.json).

### Build

```
cd wp81BmsTool
cmake --preset arm32-windows
cmake --build build
```

The executable is written to `wp81BmsTool/build/wp81BmsTool.exe`.

## Running on the device

Copy `wp81BmsTool.exe` to the phone and run it from a command-line shell on the device. Opening `\\.\QCOMPMICBMS` needs enough privileges; if it fails with `ERROR_ACCESS_DENIED`, run the tool from a shell with higher privileges.

## Project layout

```
wp81BmsTool/
├── CMakeLists.txt       Build definition (SDK include and library paths)
├── CMakePresets.json    arm32-windows preset (clang-cl, lld-link, Ninja)
└── src/
    └── main.cpp         The whole tool
```
