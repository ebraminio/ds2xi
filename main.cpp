// main.cpp — All-in-one DualSense → XInput proxy DLL
//
// Built as XInput1_3.dll and placed next to a game executable.
// The game loads this DLL instead of the system XInput1_3.dll.
// It reads a DualSense controller via hidapi and serves XInput state.
// All other XInput calls are forwarded to the real system DLL.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlwapi.h>
#include <hidapi.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include "crc32.h"

// ---------------------------------------------------------------------------
// XInput type definitions
// ---------------------------------------------------------------------------

#define XINPUT_GAMEPAD_DPAD_UP          0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN        0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT        0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT       0x0008
#define XINPUT_GAMEPAD_START            0x0010
#define XINPUT_GAMEPAD_BACK             0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB       0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB      0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER    0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER   0x0200
#define XINPUT_GAMEPAD_GUIDE            0x0400
#define XINPUT_GAMEPAD_A                0x1000
#define XINPUT_GAMEPAD_B                0x2000
#define XINPUT_GAMEPAD_X                0x4000
#define XINPUT_GAMEPAD_Y                0x8000

#define XINPUT_CAPS_FFB_SUPPORTED       0x0001
#define XINPUT_FLAG_GAMEPAD             0x00000001
#define XINPUT_DEVTYPE_GAMEPAD          0x01
#define XINPUT_DEVSUBTYPE_GAMEPAD       0x01

typedef struct _XINPUT_GAMEPAD
{
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} XINPUT_GAMEPAD, *PXINPUT_GAMEPAD;

typedef struct _XINPUT_STATE
{
    DWORD          dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE, *PXINPUT_STATE;

typedef struct _XINPUT_VIBRATION
{
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} XINPUT_VIBRATION, *PXINPUT_VIBRATION;

typedef struct _XINPUT_CAPABILITIES
{
    BYTE           Type;
    BYTE           SubType;
    WORD           Flags;
    XINPUT_GAMEPAD Gamepad;
    XINPUT_VIBRATION Vibration;
} XINPUT_CAPABILITIES, *PXINPUT_CAPABILITIES;

// ---------------------------------------------------------------------------
// System XInput function pointers
// ---------------------------------------------------------------------------

typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* PFN_XInputSetState)(DWORD, XINPUT_VIBRATION*);
typedef DWORD(WINAPI* PFN_XInputGetCapabilities)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef void (WINAPI* PFN_XInputEnable)(BOOL);
typedef DWORD(WINAPI* PFN_XInputGetDSoundAudioDeviceGuids)(DWORD, GUID*, GUID*);
typedef DWORD(WINAPI* PFN_XInputGetBatteryInformation)(DWORD, BYTE, void*);
typedef DWORD(WINAPI* PFN_XInputGetKeystroke)(DWORD, DWORD, void*);

static HMODULE                     g_SystemXInput = nullptr;
static PFN_XInputGetState          g_FpnGetState = nullptr;
static PFN_XInputSetState          g_FpnSetState = nullptr;
static PFN_XInputGetCapabilities   g_FpnGetCaps = nullptr;
static PFN_XInputEnable            g_FpnEnable = nullptr;
static PFN_XInputGetDSoundAudioDeviceGuids g_FpnGetDSound = nullptr;
static PFN_XInputGetBatteryInformation g_FpnGetBattery = nullptr;
static PFN_XInputGetKeystroke      g_FpnGetKeystroke = nullptr;

// ---------------------------------------------------------------------------
// DualSense state
// ---------------------------------------------------------------------------

static hid_device* g_DsDevice = nullptr;
static bool        g_DsConnected = false;
static bool        g_DsIsBluetooth = false;
static DWORD       g_PacketNumber = 0;

// Latest XInput state (written by PollDualSenseInput, read by XInputGetState)
static XINPUT_STATE g_LastState = {};

// Rumble state
static uint8_t g_SmallMotor = 0;
static uint8_t g_LargeMotor = 0;

// LED / color state
static uint8_t g_LedNumber = 0;
static DWORD  g_Color = 0x0000FF; // Default blue
static uint8_t g_BatteryLevel = 0;

// DualSense constants
static constexpr int SONY_VID = 0x054c;
static constexpr int DS_PID = 0x0ce6;
static constexpr int DS_EDGE_PID = 0x0df2;

static constexpr unsigned USB_BUFFER_SIZE = 64;
static constexpr unsigned BT_PAYLOAD_BUFFER_SIZE = 74;
static constexpr unsigned BT_BUFFER_SIZE = 547;
static constexpr unsigned BT_REPORT_ID = 0x31;

// ---------------------------------------------------------------------------
// Load system XInput
// ---------------------------------------------------------------------------

static bool LoadSystemXInput()
{
    if (g_SystemXInput)
        return true;

    char sysDir[MAX_PATH] = {};
    if (GetSystemDirectoryA(sysDir, MAX_PATH) == 0)
        return false;

    char fullPath[MAX_PATH] = {};
    if (!PathCombineA(fullPath, sysDir, "XInput1_3.dll"))
        return false;

    g_SystemXInput = LoadLibraryA(fullPath);
    if (!g_SystemXInput)
        return false;

    g_FpnGetState = reinterpret_cast<PFN_XInputGetState>(
        GetProcAddress(g_SystemXInput, "XInputGetState"));
    g_FpnSetState = reinterpret_cast<PFN_XInputSetState>(
        GetProcAddress(g_SystemXInput, "XInputSetState"));
    g_FpnGetCaps = reinterpret_cast<PFN_XInputGetCapabilities>(
        GetProcAddress(g_SystemXInput, "XInputGetCapabilities"));
    g_FpnEnable = reinterpret_cast<PFN_XInputEnable>(
        GetProcAddress(g_SystemXInput, "XInputEnable"));
    g_FpnGetDSound = reinterpret_cast<PFN_XInputGetDSoundAudioDeviceGuids>(
        GetProcAddress(g_SystemXInput, "XInputGetDSoundAudioDeviceGuids"));
    g_FpnGetBattery = reinterpret_cast<PFN_XInputGetBatteryInformation>(
        GetProcAddress(g_SystemXInput, "XInputGetBatteryInformation"));
    g_FpnGetKeystroke = reinterpret_cast<PFN_XInputGetKeystroke>(
        GetProcAddress(g_SystemXInput, "XInputGetKeystroke"));

    return true;
}

// ---------------------------------------------------------------------------
// DualSense output report (rumble + LED)
// ---------------------------------------------------------------------------

static void AddCrcToBuffer(uint8_t* buffer)
{
    if (!g_DsIsBluetooth)
        return;
    buffer[0] = BT_REPORT_ID;
    const uint32_t crc = computeCRC32(buffer, BT_PAYLOAD_BUFFER_SIZE);
    buffer[BT_PAYLOAD_BUFFER_SIZE + 0] = (crc >> 0) & 0xFF;
    buffer[BT_PAYLOAD_BUFFER_SIZE + 1] = (crc >> 8) & 0xFF;
    buffer[BT_PAYLOAD_BUFFER_SIZE + 2] = (crc >> 16) & 0xFF;
    buffer[BT_PAYLOAD_BUFFER_SIZE + 3] = (crc >> 24) & 0xFF;
}

static void SendDualSenseOutput()
{
    if (!g_DsDevice)
        return;

    uint8_t buffer[BT_BUFFER_SIZE];
    ZeroMemory(buffer, g_DsIsBluetooth ? BT_BUFFER_SIZE : USB_BUFFER_SIZE);

    buffer[0 + g_DsIsBluetooth] = 0x02;
    buffer[1 + g_DsIsBluetooth] = 0x03 | 0x04 | 0x08;
    buffer[2 + g_DsIsBluetooth] = 0x55;

    buffer[3 + g_DsIsBluetooth] = g_SmallMotor;
    buffer[4 + g_DsIsBluetooth] = g_LargeMotor;

    buffer[39 + g_DsIsBluetooth] = 0x02;
    buffer[42 + g_DsIsBluetooth] = 0x02;
    buffer[43 + g_DsIsBluetooth] = 0x02;

    buffer[45 + g_DsIsBluetooth] = (g_Color >> 0) & 0xFF;
    buffer[46 + g_DsIsBluetooth] = (g_Color >> 8) & 0xFF;
    buffer[47 + g_DsIsBluetooth] = (g_Color >> 16) & 0xFF;

    if (g_LedNumber == 0)
        buffer[44 + g_DsIsBluetooth] = (g_BatteryLevel == 0) ? 0
            : (g_BatteryLevel >= 100) ? 31
            : (1 << ((g_BatteryLevel * 5) / 100)) - 1;
    else
        buffer[44 + g_DsIsBluetooth] = g_LedNumber;

    AddCrcToBuffer(buffer);
    hid_write(g_DsDevice, buffer, g_DsIsBluetooth ? BT_BUFFER_SIZE : USB_BUFFER_SIZE);
}

// ---------------------------------------------------------------------------
// DualSense enumeration
// ---------------------------------------------------------------------------

static bool IsDualSenseDevice(hid_device_info* info)
{
    return info->vendor_id == SONY_VID &&
           (info->product_id == DS_PID || info->product_id == DS_EDGE_PID);
}

static void TryConnectDualSense()
{
    if (g_DsConnected)
        return;

    hid_device_info* devices = hid_enumerate(SONY_VID, 0);
    if (!devices)
        return;

    for (hid_device_info* cur = devices; cur; cur = cur->next)
    {
        if (!IsDualSenseDevice(cur))
            continue;

        hid_device* dev = hid_open_path(cur->path);
        if (!dev)
            continue;

        g_DsDevice = dev;
        g_DsIsBluetooth = cur->interface_number == -1;
        g_DsConnected = true;
        g_BatteryLevel = 0;

        hid_set_nonblocking(dev, true);
        SendDualSenseOutput();

        hid_free_enumeration(devices);
        return;
    }

    hid_free_enumeration(devices);
}

// ---------------------------------------------------------------------------
// DualSense input → XInput mapping
// ---------------------------------------------------------------------------

static void PollDualSenseInput()
{
    if (!g_DsDevice)
        return;

    uint8_t buffer[574];
    int bytesRead = hid_read(g_DsDevice, buffer, sizeof(buffer));

    if (bytesRead <= 0)
        return;

    if (bytesRead < 55)
        return;

    bool bt = g_DsIsBluetooth;
    int off = bt ? 1 : 0;

    // Battery level
    uint8_t newBattery = static_cast<uint8_t>(
        std::min((buffer[53 + off] & 15) * 12.5, 100.0));
    if (newBattery != g_BatteryLevel)
    {
        g_BatteryLevel = newBattery;
        SendDualSenseOutput();
    }

    // Build XInput state
    XINPUT_STATE state{};
    state.dwPacketNumber = ++g_PacketNumber;

    state.Gamepad.sThumbLX = static_cast<SHORT>((buffer[1 + off] * 257) - 32768);
    state.Gamepad.sThumbLY = static_cast<SHORT>(32767 - (buffer[2 + off] * 257));
    state.Gamepad.sThumbRX = static_cast<SHORT>((buffer[3 + off] * 257) - 32768);
    if (buffer[10 + off] & (1 << 2))
        state.Gamepad.sThumbRY = static_cast<SHORT>(32767 - (buffer[4 + off] * 257));

    state.Gamepad.bLeftTrigger = buffer[5 + off];
    state.Gamepad.bRightTrigger = buffer[6 + off];

    if (buffer[8 + off] & (1 << 4))
        state.Gamepad.wButtons |= XINPUT_GAMEPAD_X;
    if (buffer[8 + off] & (1 << 5))
        state.Gamepad.wButtons |= XINPUT_GAMEPAD_A;
    if (buffer[8 + off] & (1 << 6))
        state.Gamepad.wButtons |= XINPUT_GAMEPAD_B;
    if (buffer[8 + off] & (1 << 7))
        state.Gamepad.wButtons |= XINPUT_GAMEPAD_Y;
    if (buffer[9 + off] & (1 << 0))
        state.Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (buffer[9 + off] & (1 << 1))
        state.Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (buffer[9 + off] & (1 << 4))
        state.Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;
    if (buffer[9 + off] & (1 << 5))
        state.Gamepad.wButtons |= XINPUT_GAMEPAD_START;
    if (buffer[9 + off] & (1 << 6))
        state.Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
    if (buffer[9 + off] & (1 << 7))
        state.Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
    if (buffer[10 + off] & (1 << 0))
        state.Gamepad.wButtons |= XINPUT_GAMEPAD_GUIDE;

    uint8_t dpad = buffer[8 + off] & 0x0f;
    switch (dpad)
    {
    case 0: state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP; break;
    case 1: state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT; break;
    case 2: state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT; break;
    case 3: state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT; break;
    case 4: state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN; break;
    case 5: state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT; break;
    case 6: state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT; break;
    case 7: state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT; break;
    }

    // Store for XInputGetState to return
    g_LastState = state;
}

// ---------------------------------------------------------------------------
// Background polling thread
// ---------------------------------------------------------------------------

static HANDLE g_PollThread = nullptr;
static volatile bool g_Running = false;

static DWORD WINAPI PollThreadFunc(LPVOID)
{
    while (g_Running)
    {
        if (!g_DsConnected)
        {
            TryConnectDualSense();
        }
        else
        {
            PollDualSenseInput();
        }
        Sleep(1); // ~1000 Hz polling
    }
    return 0;
}

// ---------------------------------------------------------------------------
// DLL entry point
// ---------------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        LoadSystemXInput();
        hid_init();
        g_Running = true;
        g_PollThread = CreateThread(nullptr, 0, PollThreadFunc, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        g_Running = false;
        if (g_PollThread)
        {
            WaitForSingleObject(g_PollThread, 1000);
            CloseHandle(g_PollThread);
        }
        if (g_DsDevice)
        {
            // Send clean state (no rumble)
            g_SmallMotor = 0;
            g_LargeMotor = 0;
            SendDualSenseOutput();
            hid_close(g_DsDevice);
            g_DsDevice = nullptr;
        }
        hid_exit();
        if (g_SystemXInput)
        {
            FreeLibrary(g_SystemXInput);
            g_SystemXInput = nullptr;
        }
        break;
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// Exported XInput functions
// ---------------------------------------------------------------------------

extern "C" {

// Ordinal 2
__declspec(dllexport) DWORD WINAPI XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    if (dwUserIndex == 0 && g_DsConnected)
    {
        if (!pState)
            return ERROR_INVALID_PARAMETER;

        // Poll once more for freshness
        PollDualSenseInput();
        *pState = g_LastState;
        return ERROR_SUCCESS;
    }

    if (g_FpnGetState)
        return g_FpnGetState(dwUserIndex, pState);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 3
__declspec(dllexport) DWORD WINAPI XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
{
    if (dwUserIndex == 0 && g_DsConnected)
    {
        if (!pVibration)
            return ERROR_INVALID_PARAMETER;

        g_SmallMotor = pVibration->wRightMotorSpeed > 0 ? 0xFF : 0;
        g_LargeMotor = static_cast<uint8_t>(
            (static_cast<float>(pVibration->wLeftMotorSpeed) / 65535.0f) * 255.0f);
        g_LedNumber = 0;
        SendDualSenseOutput();
        return ERROR_SUCCESS;
    }

    if (g_FpnSetState)
        return g_FpnSetState(dwUserIndex, pVibration);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 4
__declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities)
{
    if (dwUserIndex == 0 && g_DsConnected)
    {
        if (!pCapabilities)
            return ERROR_INVALID_PARAMETER;
        if (dwFlags != 0 && dwFlags != XINPUT_FLAG_GAMEPAD)
            return ERROR_BAD_ARGUMENTS;

        ZeroMemory(pCapabilities, sizeof(XINPUT_CAPABILITIES));
        pCapabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
        pCapabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        pCapabilities->Flags = XINPUT_CAPS_FFB_SUPPORTED;
        pCapabilities->Gamepad.wButtons = (
            XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
            XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT |
            XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK |
            XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB |
            XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER |
            XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B |
            XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y);
        pCapabilities->Gamepad.bLeftTrigger = 0xFF;
        pCapabilities->Gamepad.bRightTrigger = 0xFF;
        pCapabilities->Gamepad.sThumbLX = -0x40;
        pCapabilities->Gamepad.sThumbLY = -0x40;
        pCapabilities->Gamepad.sThumbRX = -0x40;
        pCapabilities->Gamepad.sThumbRY = -0x40;
        pCapabilities->Vibration.wLeftMotorSpeed = 0xFFFF;
        pCapabilities->Vibration.wRightMotorSpeed = 0xFFFF;
        return ERROR_SUCCESS;
    }

    if (g_FpnGetCaps)
        return g_FpnGetCaps(dwUserIndex, dwFlags, pCapabilities);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 5
__declspec(dllexport) void WINAPI XInputEnable(BOOL enable)
{
    if (g_FpnEnable)
        g_FpnEnable(enable);
}

// Ordinal 6
__declspec(dllexport) DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD dwUserIndex, GUID* pDSoundRenderGuid, GUID* pDSoundCaptureGuid)
{
    if (g_FpnGetDSound)
        return g_FpnGetDSound(dwUserIndex, pDSoundRenderGuid, pDSoundCaptureGuid);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 7
__declspec(dllexport) DWORD WINAPI XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType, void* pBatteryInformation)
{
    if (g_FpnGetBattery)
        return g_FpnGetBattery(dwUserIndex, devType, pBatteryInformation);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 8
__declspec(dllexport) DWORD WINAPI XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, void* pKeystroke)
{
    if (g_FpnGetKeystroke)
        return g_FpnGetKeystroke(dwUserIndex, dwReserved, pKeystroke);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 100 — XInputGetStateEx (undocumented)
__declspec(dllexport) DWORD WINAPI XInputGetStateEx(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    return XInputGetState(dwUserIndex, pState);
}

} // extern "C"
