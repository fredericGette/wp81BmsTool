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
| `-o n` | Output buffer size in bytes. The default depends on the IOCTL (4 if unknown). |
| `-i n` | Input buffer size in bytes. The input is zero-padded or truncated to this size. |
| `-d path` | Device path. The default is `\\.\QCOMPMICBMS`. |
| `list` | Prints the known IOCTLs. |

Unknown IOCTL codes can be sent too; the output is then shown as a raw hex dump and as dwords.

### Examples

```
wp81BmsTool percent
wp81BmsTool 0x80180FA4
wp81BmsTool SET_SYSTEM_INFO 0 25 0
wp81BmsTool -o 16 calc
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
  0000: 4B 00 00 00                                      K...
As dwords:     hex    unsigned       signed
  [ 0] 0x0000004B          75           75
GET_PERCENT_CHARGE = 75 (0x0000004B)
```

## Known IOCTLs

All codes use device type `0x8018`, functions 1000-1008, `METHOD_BUFFERED` and `FILE_ANY_ACCESS`.

| Code | Name | Alias | Out bytes | Inputs | Notes |
|---|---|---|---|---|---|
| `0x80180FA0` | `GET_BATTERY_CHARGING_PROFILE` | `profile` | 4 | none | Backend unconfirmed; may just return the constant 4. |
| `0x80180FA4` | `GET_BATTERY_CURRENT` | `current` | 4 | none | Calibrated battery current in mA (signed). |
| `0x80180FA8` | `GET_BATTERY_VOLTAGE` | `voltage` | 4 | none | Divided VBAT at the ADC in mV, not the battery voltage itself. |
| `0x80180FAC` | `GET_PERCENT_CHARGE` | `percent` | 4 | none | State of charge as computed by the driver. |
| `0x80180FB0` | `SET_CHARGING_STATE` | `charging` | 4 | `<state>` | Forwards the charging state to the PMIC. |
| `0x80180FB4` | `SET_SYSTEM_INFO` | `sysinfo` | 4 | `<timestamp> <temperature> <batteryId>` | The temperature feeds the SOC curves. |
| `0x80180FB8` | `FORCE_OCV` | `ocv` | 4 | none | Reconfigures the BMS block (clear, wait 70 ms, configure). |
| `0x80180FBC` | `SET_XOADC_CAL_VAL` | `xoadc` | 4 | `<rawPointA> <rawPointB>` | Two-point XOADC calibration codes. |
| `0x80180FC0` | `GET_INTERNAL_CALC` | `calc` | 12 | none | Derated FCC, remaining-charge reference and headroom. |

> [!WARNING]
> The `SET_*` and `FORCE_OCV` requests change the state of the PMIC. Use them with care on a real device.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | The IOCTL succeeded (or `list` / help was printed). |
| 1 | Invalid command line. |
| 2 | The device could not be opened. |
| 3 | `DeviceIoControl` failed. |

Common errors are printed with an explanation, for example `ERROR_FILE_NOT_FOUND` when the driver is not loaded, or `ERROR_INVALID_FUNCTION` when the driver rejects the IOCTL.

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
