#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <shlwapi.h>
#include <Xinput.h>
#include <cstdint>

// XINPUT_GAMEPAD_GUIDE is not defined in the official Xinput.h
#define XINPUT_GAMEPAD_GUIDE 0x0400
static UINT g_JoyId = 0;
static bool g_JoyConnected = false;
static JOYCAPSW g_JoyCaps;
static DWORD g_PacketNumber = 0;

static void TryConnectJoystick()
{
    if (g_JoyConnected)
        return;

    const UINT count = joyGetNumDevs();
    for (UINT id = 0; id < count; ++id)
    {
        JOYINFOEX info;
        info.dwSize = sizeof(info);
        info.dwFlags = JOY_RETURNBUTTONS;
        if (joyGetPosEx(id, &info) != JOYERR_NOERROR)
            continue;
        if (joyGetDevCapsW(id, &g_JoyCaps, sizeof(g_JoyCaps)) != JOYERR_NOERROR)
            continue;

        g_JoyId = id;
        g_JoyConnected = true;
        return;
    }
}

// Scales a raw axis into 0..range using the range the driver reports.
static uint32_t ScaleAxis(DWORD value, UINT lo, UINT hi, uint32_t range)
{
    if (hi <= lo)
        return 0;
    if (value < lo)
        value = lo;
    if (value > hi)
        value = hi;
    return (value - lo) * range / (hi - lo);
}

static bool JoystickGetState(XINPUT_STATE &state)
{
    if (!g_JoyConnected)
        return false;

    JOYINFOEX info;
    info.dwSize = sizeof(info);
    info.dwFlags = JOY_RETURNALL;
    if (joyGetPosEx(g_JoyId, &info) != JOYERR_NOERROR)
    {
        g_JoyConnected = false;
        return false;
    }

    SecureZeroMemory(&state, sizeof(state));
    state.dwPacketNumber = ++g_PacketNumber;

    const JOYCAPSW &c = g_JoyCaps;
    state.Gamepad.sThumbLX = static_cast<SHORT>(static_cast<int>(ScaleAxis(info.dwXpos, c.wXmin, c.wXmax, 65535)) - 32768);
    state.Gamepad.sThumbLY = static_cast<SHORT>(32767 - static_cast<int>(ScaleAxis(info.dwYpos, c.wYmin, c.wYmax, 65535)));
    state.Gamepad.sThumbRX = static_cast<SHORT>(static_cast<int>(ScaleAxis(info.dwZpos, c.wZmin, c.wZmax, 65535)) - 32768);
    state.Gamepad.sThumbRY = static_cast<SHORT>(32767 - static_cast<int>(ScaleAxis(info.dwRpos, c.wRmin, c.wRmax, 65535)));
    state.Gamepad.bLeftTrigger = static_cast<BYTE>(ScaleAxis(info.dwVpos, c.wVmin, c.wVmax, 255));
    state.Gamepad.bRightTrigger = static_cast<BYTE>(ScaleAxis(info.dwUpos, c.wUmin, c.wUmax, 255));

    static const struct
    {
        DWORD joyButton;
        WORD xinputButton;
    } buttonMap[] = {
        {1u << 0, XINPUT_GAMEPAD_X},              // square
        {1u << 1, XINPUT_GAMEPAD_A},              // cross
        {1u << 2, XINPUT_GAMEPAD_B},              // circle
        {1u << 3, XINPUT_GAMEPAD_Y},              // triangle
        {1u << 4, XINPUT_GAMEPAD_LEFT_SHOULDER},  // L1
        {1u << 5, XINPUT_GAMEPAD_RIGHT_SHOULDER}, // R1
        {1u << 8, XINPUT_GAMEPAD_BACK},           // create
        {1u << 9, XINPUT_GAMEPAD_START},          // options
        {1u << 10, XINPUT_GAMEPAD_LEFT_THUMB},    // L3
        {1u << 11, XINPUT_GAMEPAD_RIGHT_THUMB},   // R3
        {1u << 12, XINPUT_GAMEPAD_GUIDE},         // PS
    };
    for (const auto &m : buttonMap)
        if (info.dwButtons & m.joyButton)
            state.Gamepad.wButtons |= m.xinputButton;

    // POV is in hundredths of a degree, 0xFFFF (or any value >= 36000) when centered
    if (info.dwPOV < 36000)
    {
        const DWORD pov = info.dwPOV;
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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    return TRUE;
}

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
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 3
DWORD WINAPI XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) noexcept
{
    if (dwUserIndex == 0)
        // winmm has no force feedback API and DualSense isn't exposing that
        // even to DInput8 so vibration is accepted and dropped.
        return ERROR_SUCCESS;
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
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 5
void WINAPI XInputEnable(BOOL enable) noexcept
{
}

// Ordinal 6
DWORD WINAPI XInputGetAudioDeviceIds(DWORD dwUserIndex, LPWSTR pRenderDeviceId, UINT *pRenderCount, LPWSTR pCaptureDeviceId, UINT *pCaptureCount) noexcept
{
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 7
DWORD WINAPI XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION *pBatteryInformation) noexcept
{
    return ERROR_DEVICE_NOT_CONNECTED;
}

// Ordinal 8
DWORD WINAPI XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke) noexcept
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
