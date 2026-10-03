#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <shlwapi.h>
#include <Xinput.h>
#include <cstdint>

// XINPUT_GAMEPAD_GUIDE is not defined in the official Xinput.h
#define XINPUT_GAMEPAD_GUIDE 0x0400

// ---------------------------------------------------------------------------
// Additional types not in the official Xinput.h
// ---------------------------------------------------------------------------

typedef struct _XINPUT_BASE_BUS_INFORMATION
{
    DWORD dwBusType;
    DWORD dwVendorId;
    DWORD dwProductId;
    DWORD dwVersionNumber;
    DWORD dwSerialNumber;
} XINPUT_BASE_BUS_INFORMATION;

#ifndef __WINE_XINPUT_H
typedef struct _XINPUT_CAPABILITIES_EX
{
    XINPUT_CAPABILITIES Capabilities;
    WORD  VendorId;
    WORD  ProductId;
    WORD  VersionNumber;
    WORD  unk1;
    DWORD unk2;
} XINPUT_CAPABILITIES_EX;
#endif

// ---------------------------------------------------------------------------
// System XInput function pointers
// ---------------------------------------------------------------------------

typedef DWORD(WINAPI *PFN_XInputGetState)(DWORD, XINPUT_STATE *) noexcept;
typedef DWORD(WINAPI *PFN_XInputSetState)(DWORD, XINPUT_VIBRATION *) noexcept;
typedef DWORD(WINAPI *PFN_XInputGetCapabilities)(DWORD, DWORD, XINPUT_CAPABILITIES *) noexcept;
typedef void(WINAPI *PFN_XInputEnable)(BOOL) noexcept;
typedef DWORD(WINAPI *PFN_XInputGetAudioDeviceIds)(DWORD, LPWSTR, UINT *, LPWSTR, UINT *) noexcept;
typedef DWORD(WINAPI *PFN_XInputGetBatteryInformation)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION *) noexcept;
typedef DWORD(WINAPI *PFN_XInputGetKeystroke)(DWORD, DWORD, PXINPUT_KEYSTROKE) noexcept;
typedef DWORD(WINAPI *PFN_XInputGetStateEx)(DWORD, XINPUT_STATE *) noexcept;
typedef DWORD(WINAPI *PFN_XInputWaitForGuideButton)(DWORD, DWORD, LPVOID) noexcept;
typedef DWORD(WINAPI *PFN_XInputCancelGuideButtonWait)(DWORD) noexcept;
typedef DWORD(WINAPI *PFN_XInputPowerOffController)(DWORD) noexcept;
typedef DWORD(WINAPI *PFN_XInputGetBaseBusInformation)(DWORD, DWORD, XINPUT_BASE_BUS_INFORMATION *) noexcept;
typedef DWORD(WINAPI *PFN_XInputGetCapabilitiesEx)(DWORD, DWORD, XINPUT_CAPABILITIES_EX *) noexcept;
typedef DWORD(WINAPI *PFN_XInputGetSystemButtons)(DWORD, XINPUT_KEYSTROKE *) noexcept;

static HMODULE g_SystemXInput = nullptr;
static PFN_XInputGetState g_FpnGetState = nullptr;
static PFN_XInputSetState g_FpnSetState = nullptr;
static PFN_XInputGetCapabilities g_FpnGetCaps = nullptr;
static PFN_XInputEnable g_FpnEnable = nullptr;
static PFN_XInputGetAudioDeviceIds g_FpnGetAudioDeviceIds = nullptr;
static PFN_XInputGetBatteryInformation g_FpnGetBattery = nullptr;
static PFN_XInputGetKeystroke g_FpnGetKeystroke = nullptr;
static PFN_XInputGetStateEx g_FpnGetStateEx = nullptr;
static PFN_XInputWaitForGuideButton g_FpnWaitForGuideButton = nullptr;
static PFN_XInputCancelGuideButtonWait g_FpnCancelGuideButtonWait = nullptr;
static PFN_XInputPowerOffController g_FpnPowerOffController = nullptr;
static PFN_XInputGetBaseBusInformation g_FpnGetBaseBusInformation = nullptr;
static PFN_XInputGetCapabilitiesEx g_FpnGetCapabilitiesEx = nullptr;
static PFN_XInputGetSystemButtons g_FpnGetSystemButtons = nullptr;

// ---------------------------------------------------------------------------
// Controller state (first attached DirectInput8 game controller, e.g. DualSense as generic HID)
// ---------------------------------------------------------------------------

static HINSTANCE g_Module = nullptr;
static IDirectInput8W *g_Di = nullptr;
static IDirectInputDevice8W *g_Dev = nullptr;
static bool g_JoyConnected = false;
static DWORD g_PacketNumber = 0;

// ---------------------------------------------------------------------------
// Load system XInput
// ---------------------------------------------------------------------------

static bool LoadSystemXInput()
{
    if (g_SystemXInput)
        return true;

    char sysDir[MAX_PATH];
    SecureZeroMemory(sysDir, sizeof(sysDir));
    if (GetSystemDirectoryA(sysDir, MAX_PATH) == 0)
        return false;

    char fullPath[MAX_PATH];
    SecureZeroMemory(fullPath, sizeof(fullPath));
    if (!PathCombineA(fullPath, sysDir, "XInput1_4.dll"))
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
    g_FpnGetAudioDeviceIds = reinterpret_cast<PFN_XInputGetAudioDeviceIds>(
        GetProcAddress(g_SystemXInput, "XInputGetAudioDeviceIds"));
    g_FpnGetBattery = reinterpret_cast<PFN_XInputGetBatteryInformation>(
        GetProcAddress(g_SystemXInput, "XInputGetBatteryInformation"));
    g_FpnGetKeystroke = reinterpret_cast<PFN_XInputGetKeystroke>(
        GetProcAddress(g_SystemXInput, "XInputGetKeystroke"));
    g_FpnGetStateEx = reinterpret_cast<PFN_XInputGetStateEx>(
        GetProcAddress(g_SystemXInput, reinterpret_cast<LPCSTR>(100)));
    g_FpnWaitForGuideButton = reinterpret_cast<PFN_XInputWaitForGuideButton>(
        GetProcAddress(g_SystemXInput, reinterpret_cast<LPCSTR>(101)));
    g_FpnCancelGuideButtonWait = reinterpret_cast<PFN_XInputCancelGuideButtonWait>(
        GetProcAddress(g_SystemXInput, reinterpret_cast<LPCSTR>(102)));
    g_FpnPowerOffController = reinterpret_cast<PFN_XInputPowerOffController>(
        GetProcAddress(g_SystemXInput, reinterpret_cast<LPCSTR>(103)));
    g_FpnGetBaseBusInformation = reinterpret_cast<PFN_XInputGetBaseBusInformation>(
        GetProcAddress(g_SystemXInput, reinterpret_cast<LPCSTR>(104)));
    g_FpnGetCapabilitiesEx = reinterpret_cast<PFN_XInputGetCapabilitiesEx>(
        GetProcAddress(g_SystemXInput, reinterpret_cast<LPCSTR>(108)));
    g_FpnGetSystemButtons = reinterpret_cast<PFN_XInputGetSystemButtons>(
        GetProcAddress(g_SystemXInput, reinterpret_cast<LPCSTR>(109)));

    return true;
}

// ---------------------------------------------------------------------------
// Joystick discovery
// ---------------------------------------------------------------------------

static BOOL CALLBACK EnumJoystickCb(LPCDIDEVICEINSTANCEW inst, LPVOID)
{
    IDirectInputDevice8W *dev = nullptr;
    if (FAILED(g_Di->CreateDevice(inst->guidInstance, &dev, nullptr)))
        return DIENUM_CONTINUE;

    if (FAILED(dev->SetDataFormat(&c_dfDIJoystick2)) ||
        FAILED(dev->SetCooperativeLevel(GetDesktopWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
    {
        dev->Release();
        return DIENUM_CONTINUE;
    }

    g_Dev = dev;
    return DIENUM_STOP;
}

static void DisconnectJoystick()
{
    if (g_Dev)
    {
        g_Dev->Unacquire();
        g_Dev->Release();
        g_Dev = nullptr;
    }
    g_JoyConnected = false;
}

static void TryConnectJoystick()
{
    if (g_JoyConnected)
        return;

    // Games poll every frame; enumerating DirectInput that often is too costly
    static DWORD lastAttempt = 0;
    const DWORD now = GetTickCount();
    if (lastAttempt != 0 && now - lastAttempt < 1000)
        return;
    lastAttempt = now;

    if (!g_Di && FAILED(DirectInput8Create(g_Module, DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                           reinterpret_cast<void **>(&g_Di), nullptr)))
        return;

    g_Di->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoystickCb, nullptr, DIEDFL_ATTACHEDONLY);
    if (g_Dev)
    {
        g_Dev->Acquire();
        g_JoyConnected = true;
    }
}

// Scales a raw axis (DirectInput default range 0..65535) into 0..range.
static uint32_t ScaleAxis(LONG value, uint32_t range)
{
    if (value < 0)
        value = 0;
    if (value > 65535)
        value = 65535;
    return static_cast<uint32_t>(value) * range / 65535;
}

// ---------------------------------------------------------------------------
// DirectInput8 -> XInput mapping (called directly from XInputGetState)
//
// Layout seen in joy.cpl for the DualSense: X/Y = left stick, Z/Z rotation = right stick,
// X/Y rotation = L2/R2 (also buttons 7/8, ignored), POV = d-pad.
// ---------------------------------------------------------------------------

static bool JoystickGetState(XINPUT_STATE &state)
{
    if (!g_JoyConnected)
        return false;

    HRESULT hr = g_Dev->Poll();
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
    {
        hr = g_Dev->Acquire();
        if (SUCCEEDED(hr))
            hr = g_Dev->Poll();
    }

    DIJOYSTATE2 js;
    if (FAILED(hr) || FAILED(g_Dev->GetDeviceState(sizeof(js), &js)))
    {
        DisconnectJoystick();
        return false;
    }

    SecureZeroMemory(&state, sizeof(state));
    state.dwPacketNumber = ++g_PacketNumber;

    state.Gamepad.sThumbLX = static_cast<SHORT>(static_cast<int>(ScaleAxis(js.lX, 65535)) - 32768);
    state.Gamepad.sThumbLY = static_cast<SHORT>(32767 - static_cast<int>(ScaleAxis(js.lY, 65535)));
    state.Gamepad.sThumbRX = static_cast<SHORT>(static_cast<int>(ScaleAxis(js.lZ, 65535)) - 32768);
    state.Gamepad.sThumbRY = static_cast<SHORT>(32767 - static_cast<int>(ScaleAxis(js.lRz, 65535)));
    state.Gamepad.bLeftTrigger = static_cast<BYTE>(ScaleAxis(js.lRx, 255));
    state.Gamepad.bRightTrigger = static_cast<BYTE>(ScaleAxis(js.lRy, 255));

    static const struct
    {
        int joyButton;
        WORD xinputButton;
    } buttonMap[] = {
        {0, XINPUT_GAMEPAD_X},              // square
        {1, XINPUT_GAMEPAD_A},              // cross
        {2, XINPUT_GAMEPAD_B},              // circle
        {3, XINPUT_GAMEPAD_Y},              // triangle
        {4, XINPUT_GAMEPAD_LEFT_SHOULDER},  // L1
        {5, XINPUT_GAMEPAD_RIGHT_SHOULDER}, // R1
        {8, XINPUT_GAMEPAD_BACK},           // create
        {9, XINPUT_GAMEPAD_START},          // options
        {10, XINPUT_GAMEPAD_LEFT_THUMB},    // L3
        {11, XINPUT_GAMEPAD_RIGHT_THUMB},   // R3
        {12, XINPUT_GAMEPAD_GUIDE},         // PS
    };
    for (const auto &m : buttonMap)
    {
        if (js.rgbButtons[m.joyButton] & 0x80)
            state.Gamepad.wButtons |= m.xinputButton;
    }

    // POV is in hundredths of a degree, LOWORD 0xFFFF when centered
    if (LOWORD(js.rgdwPOV[0]) != 0xFFFF)
    {
        const DWORD pov = js.rgdwPOV[0];
        if (pov >= 31500 || pov <= 4500)
            state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
        if (pov >= 4500 && pov <= 13500)
            state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
        if (pov >= 13500 && pov <= 22500)
            state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (pov >= 22500 && pov <= 31500)
            state.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
    }

    return true;
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
        g_Module = hModule;
        LoadSystemXInput();
        break;

    case DLL_PROCESS_DETACH:
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
//
// Note: We do NOT wrap these in extern "C" because <Xinput.h> already
// declares them with C linkage. We also avoid __declspec(dllexport)
// and use a .def file instead to prevent linkage conflicts.
// ---------------------------------------------------------------------------

// Ordinal 2
DWORD WINAPI XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState) noexcept
{
    if (dwUserIndex == 0)
    {
        if (!pState)
            return ERROR_INVALID_PARAMETER;

        if (!g_JoyConnected)
            TryConnectJoystick();

        if (g_JoyConnected && JoystickGetState(*pState))
            return ERROR_SUCCESS;
    }

    if (g_FpnGetState)
        return g_FpnGetState(dwUserIndex, pState);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 3
DWORD WINAPI XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) noexcept
{
    if (dwUserIndex == 0)
    {
        if (!pVibration)
            return ERROR_INVALID_PARAMETER;

        if (!g_JoyConnected)
            TryConnectJoystick();

        // DirectInput has no force feedback for this device, so vibration is accepted and dropped
        if (g_JoyConnected)
            return ERROR_SUCCESS;
    }

    if (g_FpnSetState)
        return g_FpnSetState(dwUserIndex, pVibration);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 4
DWORD WINAPI XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities) noexcept
{
    if (dwUserIndex == 0)
    {
        if (!pCapabilities)
            return ERROR_INVALID_PARAMETER;
        if (dwFlags != 0 && dwFlags != XINPUT_FLAG_GAMEPAD)
            return ERROR_BAD_ARGUMENTS;

        if (!g_JoyConnected)
            TryConnectJoystick();

        if (g_JoyConnected)
        {
            SecureZeroMemory(pCapabilities, sizeof(XINPUT_CAPABILITIES));
            pCapabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
            pCapabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
            pCapabilities->Flags = XINPUT_CAPS_FFB_SUPPORTED;
            pCapabilities->Gamepad.wButtons = (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
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
    }

    if (g_FpnGetCaps)
        return g_FpnGetCaps(dwUserIndex, dwFlags, pCapabilities);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 5
void WINAPI XInputEnable(BOOL enable) noexcept
{
    if (g_FpnEnable)
        g_FpnEnable(enable);
}

// Ordinal 6
DWORD WINAPI XInputGetAudioDeviceIds(DWORD dwUserIndex, LPWSTR pRenderDeviceId, UINT *pRenderCount, LPWSTR pCaptureDeviceId, UINT *pCaptureCount) noexcept
{
    if (g_FpnGetAudioDeviceIds)
        return g_FpnGetAudioDeviceIds(dwUserIndex, pRenderDeviceId, pRenderCount, pCaptureDeviceId, pCaptureCount);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 7
DWORD WINAPI XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION *pBatteryInformation) noexcept
{
    if (dwUserIndex == 0)
    {
        if (pBatteryInformation)
        {
            pBatteryInformation->BatteryType = BATTERY_TYPE_WIRED;
            pBatteryInformation->BatteryLevel = BATTERY_LEVEL_FULL;
            return ERROR_SUCCESS;
        }
    }

    if (g_FpnGetBattery)
        return g_FpnGetBattery(dwUserIndex, devType, pBatteryInformation);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 8
DWORD WINAPI XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke) noexcept
{
    if (g_FpnGetKeystroke)
        return g_FpnGetKeystroke(dwUserIndex, dwReserved, pKeystroke);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 100 — XInputGetStateEx (undocumented)
DWORD WINAPI XInputGetStateEx(DWORD dwUserIndex, XINPUT_STATE *pState) noexcept
{
    return XInputGetState(dwUserIndex, pState);
}

// Ordinal 101 — XInputWaitForGuideButton (undocumented)
DWORD WINAPI XInputWaitForGuideButton(DWORD dwUserIndex, DWORD dwFlag, LPVOID pVoid) noexcept
{
    if (g_FpnWaitForGuideButton)
        return g_FpnWaitForGuideButton(dwUserIndex, dwFlag, pVoid);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 102 — XInputCancelGuideButtonWait (undocumented)
DWORD WINAPI XInputCancelGuideButtonWait(DWORD dwUserIndex) noexcept
{
    if (g_FpnCancelGuideButtonWait)
        return g_FpnCancelGuideButtonWait(dwUserIndex);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 103 — XInputPowerOffController (undocumented)
DWORD WINAPI XInputPowerOffController(DWORD dwUserIndex) noexcept
{
    if (g_FpnPowerOffController)
        return g_FpnPowerOffController(dwUserIndex);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 104 — XInputGetBaseBusInformation (undocumented)
DWORD WINAPI XInputGetBaseBusInformation(DWORD dwUserIndex, DWORD dwFlags, XINPUT_BASE_BUS_INFORMATION *pBaseBusInformation) noexcept
{
    if (g_FpnGetBaseBusInformation)
        return g_FpnGetBaseBusInformation(dwUserIndex, dwFlags, pBaseBusInformation);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 108 — XInputGetCapabilitiesEx (undocumented)
DWORD WINAPI XInputGetCapabilitiesEx(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES_EX *pCapabilitiesEx) noexcept
{
    if (g_FpnGetCapabilitiesEx)
        return g_FpnGetCapabilitiesEx(dwUserIndex, dwFlags, pCapabilitiesEx);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 109 — XInputGetSystemButtons (undocumented)
DWORD WINAPI XInputGetSystemButtons(DWORD dwUserIndex, XINPUT_KEYSTROKE *pKeystroke) noexcept
{
    if (g_FpnGetSystemButtons)
        return g_FpnGetSystemButtons(dwUserIndex, pKeystroke);
    return ERROR_DEVICE_NOT_CONNECTED;
}
