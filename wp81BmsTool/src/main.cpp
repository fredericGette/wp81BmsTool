// wp81BmsTool - sends one IOCTL to the Qualcomm PMIC BMS driver (\\.\QCOMPMICBMS)
// and prints the result.
//
// Usage: wp81BmsTool [options] <ioctl> [input dword ...]
//   <ioctl>   numeric code (0x80180FAC) or name (GET_PERCENT_CHARGE, percent, ...)
//   input     32-bit values, decimal or 0x-hex, negative allowed
// Options:
//   -o <n>    output buffer size in bytes (default depends on the IOCTL)
//   -i <n>    input buffer size in bytes (zero padded / truncated)
//   -d <path> device path (default \\.\QCOMPMICBMS)
//   list      print the known IOCTLs

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CreateFileW and DeviceIoControl are exported by kernelbase (mincore.lib) on
// Windows Phone 8.1, but the SDK headers only declare them for the desktop
// partition.
extern "C" {
WINBASEAPI HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess,
                                     DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                                     DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
                                     HANDLE hTemplateFile);
WINBASEAPI BOOL WINAPI DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode,
                                       LPVOID lpInBuffer, DWORD nInBufferSize,
                                       LPVOID lpOutBuffer, DWORD nOutBufferSize,
                                       LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped);
}

#define DEFAULT_DEVICE_PATH L"\\\\.\\QCOMPMICBMS"
#define MAX_BUFFER_SIZE     4096

enum OutputKind {
    OUT_RAW,            // no specific decoding
    OUT_ULONG,
    OUT_LONG,
    OUT_INTERNAL_CALC,  // three ULONGs
};

struct IoctlInfo {
    DWORD Code;
    const char *Name;
    const char *Alias;
    DWORD DefaultOutputSize;
    OutputKind Kind;
    const char *Inputs;
    const char *Notes;
};

// IOCTL_BMS_* codes handled by PmicBmsIoctlDispatch (device type 0x8018,
// functions 1000-1008, METHOD_BUFFERED, FILE_ANY_ACCESS).
static const IoctlInfo g_Ioctls[] = {
    { 0x80180FA0, "GET_BATTERY_CHARGING_PROFILE", "profile", 4, OUT_ULONG,
      "(none)", "backend unconfirmed, may just return the constant 4" },
    { 0x80180FA4, "GET_BATTERY_CURRENT", "current", 4, OUT_LONG,
      "(none)", "calibrated battery current, mA (signed)" },
    { 0x80180FA8, "GET_BATTERY_VOLTAGE", "voltage", 4, OUT_ULONG,
      "(none)", "divided VBAT at the ADC, mV (BMS output reg, selector 6: ((raw-0x6000)*1000>>10 + 5)/10)" },
    { 0x80180FAC, "GET_PERCENT_CHARGE", "percent", 4, OUT_ULONG,
      "(none)", "state of charge as computed by the driver" },
    { 0x80180FB0, "SET_CHARGING_STATE", "charging", 4, OUT_RAW,
      "<state>", "forwards the charging state to the PMIC" },
    { 0x80180FB4, "SET_SYSTEM_INFO", "sysinfo", 4, OUT_LONG,
      "<timestamp> <temperature> <batteryId>", "temperature feeds the SOC curves" },
    { 0x80180FB8, "FORCE_OCV", "ocv", 4, OUT_ULONG,
      "(none)", "reconfigures the BMS block (clear, 70 ms, configure)" },
    { 0x80180FBC, "SET_XOADC_CAL_VAL", "xoadc", 4, OUT_RAW,
      "<rawPointA> <rawPointB>", "two-point XOADC calibration codes" },
    { 0x80180FC0, "GET_INTERNAL_CALC", "calc", 12, OUT_INTERNAL_CALC,
      "(none)", "derated FCC, remaining-charge reference, headroom" },
};

#define IOCTL_COUNT (sizeof(g_Ioctls) / sizeof(g_Ioctls[0]))

static const IoctlInfo *FindIoctlByCode(DWORD code)
{
    for (size_t i = 0; i < IOCTL_COUNT; i++) {
        if (g_Ioctls[i].Code == code) {
            return &g_Ioctls[i];
        }
    }
    return NULL;
}

static const IoctlInfo *FindIoctlByName(const char *name)
{
    if (_strnicmp(name, "IOCTL_BMS_", 10) == 0) {
        name += 10;
    }
    for (size_t i = 0; i < IOCTL_COUNT; i++) {
        if (_stricmp(g_Ioctls[i].Name, name) == 0 || _stricmp(g_Ioctls[i].Alias, name) == 0) {
            return &g_Ioctls[i];
        }
    }
    return NULL;
}

// Parses a 32-bit value: decimal, 0x-hex or negative. Returns false on garbage.
static bool ParseDword(const char *text, DWORD *value)
{
    char *end = NULL;
    __int64 v = _strtoi64(text, &end, 0);
    if (end == text || *end != '\0' || v < -0x80000000LL || v > 0xFFFFFFFFLL) {
        return false;
    }
    *value = (DWORD)v;
    return true;
}

static void PrintUsage()
{
    printf("Usage: wp81BmsTool [-o outBytes] [-i inBytes] [-d devicePath] <ioctl> [input dword ...]\n");
    printf("       wp81BmsTool list\n\n");
    printf("  <ioctl>  code (e.g. 0x80180FAC) or name (e.g. GET_PERCENT_CHARGE or percent)\n");
    printf("  input    32-bit values, decimal or 0x-hex, negative allowed\n");
    printf("  -o n     output buffer size in bytes (default depends on the IOCTL, 4 if unknown)\n");
    printf("  -i n     input buffer size in bytes (zero padded or truncated)\n");
    printf("  -d path  device path (default \\\\.\\QCOMPMICBMS)\n\n");
    printf("Examples:\n");
    printf("  wp81BmsTool percent\n");
    printf("  wp81BmsTool 0x80180FA4\n");
    printf("  wp81BmsTool SET_SYSTEM_INFO 0 25 0\n");
}

static void PrintIoctlList()
{
    printf("%-10s %-30s %-9s %-4s %s\n", "Code", "Name", "Alias", "Out", "Inputs");
    for (size_t i = 0; i < IOCTL_COUNT; i++) {
        const IoctlInfo *info = &g_Ioctls[i];
        printf("0x%08lX %-30s %-9s %-4lu %s\n", info->Code, info->Name, info->Alias,
               info->DefaultOutputSize, info->Inputs);
        printf("           %s\n", info->Notes);
    }
}

static const char *Win32ErrorName(DWORD error)
{
    switch (error) {
    case ERROR_FILE_NOT_FOUND:        return "ERROR_FILE_NOT_FOUND (driver not loaded or wrong device path)";
    case ERROR_ACCESS_DENIED:         return "ERROR_ACCESS_DENIED (run with higher privileges)";
    case ERROR_INVALID_FUNCTION:      return "ERROR_INVALID_FUNCTION (STATUS_INVALID_DEVICE_REQUEST, IOCTL rejected)";
    case ERROR_INVALID_PARAMETER:     return "ERROR_INVALID_PARAMETER";
    case ERROR_GEN_FAILURE:           return "ERROR_GEN_FAILURE (STATUS_UNSUCCESSFUL)";
    case ERROR_INSUFFICIENT_BUFFER:   return "ERROR_INSUFFICIENT_BUFFER";
    case ERROR_MORE_DATA:             return "ERROR_MORE_DATA (STATUS_BUFFER_OVERFLOW)";
    case ERROR_NOT_SUPPORTED:         return "ERROR_NOT_SUPPORTED";
    case ERROR_BAD_LENGTH:            return "ERROR_BAD_LENGTH";
    case ERROR_NOT_READY:             return "ERROR_NOT_READY";
    case ERROR_DEVICE_NOT_CONNECTED:  return "ERROR_DEVICE_NOT_CONNECTED";
    case ERROR_OPERATION_ABORTED:     return "ERROR_OPERATION_ABORTED";
    default:                          return "";
    }
}

static void PrintHexDump(const BYTE *data, DWORD length)
{
    for (DWORD offset = 0; offset < length; offset += 16) {
        printf("  %04lX:", offset);
        for (DWORD i = 0; i < 16; i++) {
            if (offset + i < length) {
                printf(" %02X", data[offset + i]);
            } else {
                printf("   ");
            }
        }
        printf("  ");
        for (DWORD i = 0; i < 16 && offset + i < length; i++) {
            BYTE c = data[offset + i];
            printf("%c", (c >= 0x20 && c < 0x7F) ? c : '.');
        }
        printf("\n");
    }
}

static void PrintDwords(const BYTE *data, DWORD length)
{
    for (DWORD offset = 0; offset + 4 <= length; offset += 4) {
        DWORD v;
        memcpy(&v, data + offset, sizeof(v));
        printf("  [%2lu] 0x%08lX  %10lu  %11ld\n", offset / 4, v, v, (LONG)v);
    }
}

static void PrintDecoded(const IoctlInfo *info, const BYTE *data, DWORD length)
{
    DWORD v[3] = { 0, 0, 0 };
    memcpy(v, data, length < sizeof(v) ? length : sizeof(v));

    switch (info->Kind) {
    case OUT_ULONG:
        if (length >= 4) {
            printf("%s = %lu (0x%08lX)\n", info->Name, v[0], v[0]);
            if (info->Code == 0x80180FA8) {
                printf("Note: divided VBAT at the ADC, in mV (not the battery voltage itself)\n");
            }
        }
        break;
    case OUT_LONG:
        if (length >= 4) {
            printf("%s = %ld (0x%08lX)\n", info->Name, (LONG)v[0], v[0]);
        }
        break;
    case OUT_INTERNAL_CALC:
        if (length >= 12) {
            printf("Derated full-charge capacity   = %lu\n", v[0]);
            printf("Remaining-charge reference     = %lu\n", v[1]);
            printf("Remaining-charge headroom      = %lu\n", v[2]);
        } else if (length > 0) {
            printf("Only %lu of 12 bytes returned; values above are partial\n", length);
        }
        break;
    case OUT_RAW:
        break;
    }
}

int main(int argc, char **argv)
{
    const wchar_t *devicePath = DEFAULT_DEVICE_PATH;
    wchar_t devicePathBuffer[MAX_PATH];
    long outputSize = -1;
    long inputSize = -1;
    int argIndex = 1;

    // Options
    while (argIndex < argc && argv[argIndex][0] == '-' && argv[argIndex][1] != '\0' &&
           !(argv[argIndex][1] >= '0' && argv[argIndex][1] <= '9')) {
        const char *opt = argv[argIndex];
        if (_stricmp(opt, "-h") == 0 || _stricmp(opt, "--help") == 0 || _stricmp(opt, "-?") == 0) {
            PrintUsage();
            return 0;
        }
        if (argIndex + 1 >= argc) {
            printf("Missing value for option %s\n", opt);
            return 1;
        }
        const char *value = argv[argIndex + 1];
        if (_stricmp(opt, "-o") == 0 || _stricmp(opt, "-i") == 0) {
            DWORD size;
            if (!ParseDword(value, &size) || size > MAX_BUFFER_SIZE) {
                printf("Invalid size for %s: %s (max %d)\n", opt, value, MAX_BUFFER_SIZE);
                return 1;
            }
            if (opt[1] == 'o' || opt[1] == 'O') {
                outputSize = (long)size;
            } else {
                inputSize = (long)size;
            }
        } else if (_stricmp(opt, "-d") == 0) {
            if (MultiByteToWideChar(CP_ACP, 0, value, -1, devicePathBuffer, MAX_PATH) == 0) {
                printf("Invalid device path: %s\n", value);
                return 1;
            }
            devicePath = devicePathBuffer;
        } else {
            printf("Unknown option: %s\n\n", opt);
            PrintUsage();
            return 1;
        }
        argIndex += 2;
    }

    if (argIndex >= argc) {
        PrintUsage();
        return 1;
    }

    if (_stricmp(argv[argIndex], "list") == 0) {
        PrintIoctlList();
        return 0;
    }

    // IOCTL code
    DWORD ioctlCode;
    const IoctlInfo *info = FindIoctlByName(argv[argIndex]);
    if (info != NULL) {
        ioctlCode = info->Code;
    } else if (ParseDword(argv[argIndex], &ioctlCode)) {
        info = FindIoctlByCode(ioctlCode);
    } else {
        printf("Unknown IOCTL: %s (use 'list' to see the known ones)\n", argv[argIndex]);
        return 1;
    }
    argIndex++;

    // Input dwords
    static BYTE inputBuffer[MAX_BUFFER_SIZE];
    static BYTE outputBuffer[MAX_BUFFER_SIZE];
    DWORD inputDwordCount = (DWORD)(argc - argIndex);
    if (inputDwordCount * 4 > MAX_BUFFER_SIZE) {
        printf("Too many input values (max %d)\n", MAX_BUFFER_SIZE / 4);
        return 1;
    }
    for (DWORD i = 0; i < inputDwordCount; i++) {
        DWORD value;
        if (!ParseDword(argv[argIndex + i], &value)) {
            printf("Invalid input value: %s\n", argv[argIndex + i]);
            return 1;
        }
        memcpy(inputBuffer + i * 4, &value, sizeof(value));
    }

    DWORD inputLength = (inputSize >= 0) ? (DWORD)inputSize : inputDwordCount * 4;
    DWORD outputLength = (outputSize >= 0) ? (DWORD)outputSize
                                           : (info != NULL ? info->DefaultOutputSize : 4);

    printf("IOCTL    : 0x%08lX %s%s\n", ioctlCode, info != NULL ? "IOCTL_BMS_" : "",
           info != NULL ? info->Name : "(unknown)");
    printf("  DeviceType 0x%04lX, Function %lu, Method %lu, Access %lu\n",
           (ioctlCode >> 16) & 0xFFFF, (ioctlCode >> 2) & 0xFFF, ioctlCode & 3,
           (ioctlCode >> 14) & 3);
    if (info != NULL && inputDwordCount == 0 && strcmp(info->Inputs, "(none)") != 0) {
        printf("  Note: this IOCTL expects inputs %s\n", info->Inputs);
    }
    printf("Input    : %lu bytes\n", inputLength);
    if (inputLength > 0) {
        PrintHexDump(inputBuffer, inputLength);
    }
    printf("Output   : %lu bytes buffer\n", outputLength);

    // Open the device
    HANDLE device = CreateFileW(devicePath, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED) {
        // The IOCTLs are FILE_ANY_ACCESS, so a handle without read/write access is enough.
        device = CreateFileW(devicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, 0, NULL);
    }
    if (device == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        printf("CreateFile(%ls) failed: %lu %s\n", devicePath, error, Win32ErrorName(error));
        return 2;
    }

    // Send the IOCTL
    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(device, ioctlCode,
                              inputLength > 0 ? inputBuffer : NULL, inputLength,
                              outputLength > 0 ? outputBuffer : NULL, outputLength,
                              &bytesReturned, NULL);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(device);

    if (!ok) {
        printf("Result   : FAILED, error %lu %s\n", error, Win32ErrorName(error));
    } else {
        printf("Result   : SUCCESS\n");
    }
    printf("Returned : %lu bytes\n", bytesReturned);

    // Print whatever was returned, even on failure (ERROR_MORE_DATA still returns data).
    DWORD shown = bytesReturned <= outputLength ? bytesReturned : outputLength;
    if (shown > 0) {
        printf("Output data:\n");
        PrintHexDump(outputBuffer, shown);
        printf("As dwords:     hex    unsigned       signed\n");
        PrintDwords(outputBuffer, shown);
        if (ok && info != NULL) {
            PrintDecoded(info, outputBuffer, shown);
        }
    }

    return ok ? 0 : 3;
}
