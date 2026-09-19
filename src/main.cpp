#ifdef NDEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif
#include <windows.h>
#include <hidapi.h>
#include <cstdio>
#include <cstdint>

#include "ViGEm/Client.h"
#include "crc32.h"

constexpr int SONY_VENDOR_ID = 0x054c;

constexpr int DUALSENSE_PRODUCT_ID = 0x0ce6;
constexpr int DUALSENSEEDGE_PRODUCT_ID = 0x0df2;
constexpr int DUALSHOCK4_PRODUCT_ID = 0x09CC;

constexpr unsigned USB_BUFFER_SIZE = 64;
constexpr unsigned BT_PAYLOAD_BUFFER_SIZE = 74;
constexpr unsigned BT_CRC_BUFFER_SIZE = 4;
constexpr unsigned BT_BUFFER_SIZE = 547;
constexpr unsigned USB_REPORT_ID = 0x01;
constexpr unsigned BT_REPORT_ID = 0x31;

// A connection between actual DualSense controller and virtual XInput device
struct ControllerBridge
{
	// Actual controller's 
	hid_device* actualControllerHandle;
	bool isActualControllerBluetooth;

	// Raw
	uint8_t inputBuffer[574];
	int bufferSize;

	// Things to get from the virtual controller and pass to the actual one
	uint8_t rumble[2];
	uint8_t ledNumber;

	// 
	int shortTriggers;
	int batteryLevel;

	// Things to pass to the actual controller 
	bool microphoneLed;

	// Virtual controller handle
	PVIGEM_TARGET virtualController;
	VIGEM_ERROR error;
};

static bool isDualsenseConnected(ControllerBridge &bridge)
{
	bridge.actualControllerHandle = hid_open(SONY_VENDOR_ID, DUALSENSE_PRODUCT_ID, NULL);
	if (bridge.actualControllerHandle == nullptr)
	{
		// Fallback to edge's PID
		bridge.actualControllerHandle = hid_open(SONY_VENDOR_ID, DUALSENSEEDGE_PRODUCT_ID, NULL);
		if (bridge.actualControllerHandle == nullptr)
		{
			printf("%ls\n", hid_error(bridge.actualControllerHandle));
			return false;
		}
	}

	hid_set_nonblocking(bridge.actualControllerHandle, true);
	bridge.isActualControllerBluetooth = hid_get_device_info(bridge.actualControllerHandle)->interface_number == -1;
	// Bluetooth
	if (bridge.isActualControllerBluetooth)
	{
		bridge.bufferSize = BT_PAYLOAD_BUFFER_SIZE + BT_CRC_BUFFER_SIZE;
		bridge.inputBuffer[0] = BT_REPORT_ID;
	}
	else
	{
		bridge.bufferSize = USB_BUFFER_SIZE;
		bridge.inputBuffer[0] = USB_REPORT_ID;
	}
	return true;
}

static void add_crc_to_buffer(uint8_t *outputHID)
{
	outputHID[0] = BT_REPORT_ID;
	const UINT32 crc = computeCRC32(outputHID, BT_PAYLOAD_BUFFER_SIZE);
	outputHID[BT_PAYLOAD_BUFFER_SIZE] = crc & 0x000000FF;
	outputHID[BT_PAYLOAD_BUFFER_SIZE + 1] = ((crc & 0x0000FF00) >> 8UL);
	outputHID[BT_PAYLOAD_BUFFER_SIZE + 2] = ((crc & 0x00FF0000) >> 16UL);
	outputHID[BT_PAYLOAD_BUFFER_SIZE + 3] = ((crc & 0xFF000000) >> 24UL);
}

constexpr bool demo = true;

static void sendDualsenseOutputReport(ControllerBridge &bridge, uint64_t counter)
{
	hid_device *deviceHandle = bridge.actualControllerHandle;
	if (!deviceHandle)
		return;

	bool isBluetooth = bridge.isActualControllerBluetooth;

	uint8_t outputHID[BT_BUFFER_SIZE];
	ZeroMemory(outputHID, isBluetooth ? BT_BUFFER_SIZE : USB_BUFFER_SIZE);

	// USB Report ID or BT additional Flag
	outputHID[0 + isBluetooth] = 0x02;

	// Trigger Flags
	outputHID[1 + isBluetooth] = 0x03 | 0x04 | 0x08;
	outputHID[2 + isBluetooth] = 0x55;

	outputHID[3 + isBluetooth] = bridge.rumble[0]; // Low Rumble
	outputHID[4 + isBluetooth] = bridge.rumble[1]; // High Rumble

	outputHID[9 + isBluetooth] = bridge.microphoneLed;

	outputHID[39 + isBluetooth] = 0x02;
	outputHID[42 + isBluetooth] = 0x02;
	outputHID[43 + isBluetooth] = 0x02;

	outputHID[44 + isBluetooth] = bridge.ledNumber;
	if (demo && bridge.ledNumber == 0)
		outputHID[44 + isBluetooth] = (counter / 400) % 2 ? 0b00100 : 0b10001;

	if (demo)
	{
		static float Red = 210, Green = 0, Blue = 90;
		if (counter % 0xF == 0)
		{
			static int AddRed = 1, AddGreen = 1, AddBlue = 1;
			if (Red == 255)
				AddRed = -1;
			if (Red == 0)
				AddRed = 1;
			if (Green == 255)
				AddGreen = -1;
			if (Green == 0)
				AddGreen = 1;
			if (Blue == 255)
				AddBlue = -1;
			if (Blue == 0)
				AddBlue = 1;
			Red += 1.5f * AddRed;
			Green += 1.5f * AddGreen;
			Blue += 1.5f * AddBlue;
		}
		outputHID[45 + isBluetooth] = Red;
		outputHID[46 + isBluetooth] = Green;
		outputHID[47 + isBluetooth] = Blue;
	}
	if (isBluetooth)
		add_crc_to_buffer(outputHID);
	hid_write(deviceHandle, outputHID, isBluetooth ? BT_BUFFER_SIZE : USB_BUFFER_SIZE);
}

static void getDualsenseInput(PVIGEM_CLIENT client, ControllerBridge &bridge, uint64_t counter)
{
	int readCount = hid_read(bridge.actualControllerHandle, bridge.inputBuffer, bridge.bufferSize);
	if (readCount == 0)
		// Non blocking read can return 0 data
		return;
	else if (readCount == -1)
	{
		printf("%ls\n", hid_read_error(bridge.actualControllerHandle));
		hid_close(bridge.actualControllerHandle);
		bridge.actualControllerHandle = nullptr;
		return;
	}

	bool isBluetooth = bridge.isActualControllerBluetooth;

	// Because of a bug on the Dualsense HID this needs to be implemented or else battery might display higher than 100 %
	bridge.batteryLevel = min((bridge.inputBuffer[53 + isBluetooth] & 15) * 12.5, 100);

	XUSB_REPORT gamepadReport{};
	gamepadReport.sThumbLX = (bridge.inputBuffer[1 + isBluetooth] * 257) - 32768;
	gamepadReport.sThumbLY = 32767 - (bridge.inputBuffer[2 + isBluetooth] * 257);
	gamepadReport.sThumbRX = (bridge.inputBuffer[3 + isBluetooth] * 257) - 32768;
	gamepadReport.sThumbRY = 32767 - (bridge.inputBuffer[4 + isBluetooth] * 257);

	gamepadReport.bLeftTrigger = bridge.inputBuffer[5 + isBluetooth] * (bridge.shortTriggers == 0) + (((bridge.inputBuffer[5 + isBluetooth]) >> 2) + 190) * (bridge.shortTriggers != 0);
	gamepadReport.bRightTrigger = bridge.inputBuffer[6 + isBluetooth] * (bridge.shortTriggers == 0) + (((bridge.inputBuffer[6 + isBluetooth]) >> 2) + 190) * (bridge.shortTriggers != 0);

	// Normal Order
	gamepadReport.wButtons = (bool)(bridge.inputBuffer[8 + isBluetooth] & (1 << 4)) ? XUSB_GAMEPAD_X : 0;               // Square
	gamepadReport.wButtons |= (bool)(bridge.inputBuffer[8 + isBluetooth] & (1 << 5)) ? XUSB_GAMEPAD_A : 0;              // Cross
	gamepadReport.wButtons |= (bool)(bridge.inputBuffer[8 + isBluetooth] & (1 << 6)) ? XUSB_GAMEPAD_B : 0;              // Circle
	gamepadReport.wButtons |= (bool)(bridge.inputBuffer[8 + isBluetooth] & (1 << 7)) ? XUSB_GAMEPAD_Y : 0;              // Triangle
	gamepadReport.wButtons |= (bool)(bridge.inputBuffer[9 + isBluetooth] & (1 << 0)) ? XUSB_GAMEPAD_LEFT_SHOULDER : 0;  // Left Shoulder
	gamepadReport.wButtons |= (bool)(bridge.inputBuffer[9 + isBluetooth] & (1 << 1)) ? XUSB_GAMEPAD_RIGHT_SHOULDER : 0; // Right Shoulder
	gamepadReport.wButtons |= (bool)(bridge.inputBuffer[9 + isBluetooth] & (1 << 4)) ? XUSB_GAMEPAD_BACK : 0;           // Select
	gamepadReport.wButtons |= (bool)(bridge.inputBuffer[9 + isBluetooth] & (1 << 5)) ? XUSB_GAMEPAD_START : 0;          // Start
	gamepadReport.wButtons |= (bool)(bridge.inputBuffer[9 + isBluetooth] & (1 << 6)) ? XUSB_GAMEPAD_LEFT_THUMB : 0;     // Left Thumb
	gamepadReport.wButtons |= (bool)(bridge.inputBuffer[9 + isBluetooth] & (1 << 7)) ? XUSB_GAMEPAD_RIGHT_THUMB : 0;    // Right thumb

	// XUSB_GAMEPAD_GUIDE is undocumented on XInput, but it is used by the Xbox button on the controller. The DualSense controller has a similar button, which is mapped to the GUIDE button in this code.
	gamepadReport.wButtons |= (bool)(bridge.inputBuffer[10 + isBluetooth] & (1 << 0)) ? XUSB_GAMEPAD_GUIDE : 0;
	// 1 << 1 => Touchpad Button
	// 1 << 2 => Mic Button
	// DualSense Edge:
	//  1 << 4 => Left Function
	//  1 << 5 => Right Function
	//  1 << 6 => Left Paddle
	//  1 << 7 => Right Paddle
	if (demo)
	{
		bridge.microphoneLed = bridge.inputBuffer[10 + isBluetooth] & (1 << 1);
		bridge.microphoneLed = bridge.inputBuffer[10 + isBluetooth] & (1 << 2);
	}

	uint8_t dpad = bridge.inputBuffer[8 + isBluetooth] & 0x0f;
	if (dpad == 0)
		gamepadReport.wButtons |= XUSB_GAMEPAD_DPAD_UP;
	else if (dpad == 1)
		gamepadReport.wButtons |= XUSB_GAMEPAD_DPAD_UP + XUSB_GAMEPAD_DPAD_RIGHT;
	else if (dpad == 2)
		gamepadReport.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT;
	else if (dpad == 3)
		gamepadReport.wButtons |= XUSB_GAMEPAD_DPAD_DOWN + XUSB_GAMEPAD_DPAD_RIGHT;
	else if (dpad == 4)
		gamepadReport.wButtons |= XUSB_GAMEPAD_DPAD_DOWN;
	else if (dpad == 5)
		gamepadReport.wButtons |= XUSB_GAMEPAD_DPAD_DOWN + XUSB_GAMEPAD_DPAD_LEFT;
	else if (dpad == 6)
		gamepadReport.wButtons |= XUSB_GAMEPAD_DPAD_LEFT;
	else if (dpad == 7)
		gamepadReport.wButtons |= XUSB_GAMEPAD_DPAD_UP + XUSB_GAMEPAD_DPAD_LEFT;

	vigem_target_x360_update(client, bridge.virtualController, gamepadReport);
}

/*
*		Triggers Documentation
*
*
		outputHID[11 + bluetooth]; //Mode Motor Right
		outputHID[12 + bluetooth]; //right trigger start of resistance section
		outputHID[13 + bluetooth]; //right trigger (mode1) amount of force exerted (mode2) end of resistance section supplemental mode 4+20) flag(s?) 0x02 = do not pause effect when fully presse
		outputHID[14 + bluetooth]; //right trigger force exerted in range (mode2)
		outputHID[15 + bluetooth]; // strength of effect near release state (requires supplement modes 4 and 20)
		outputHID[16 + bluetooth]; // strength of effect near middle (requires supplement modes 4 and 20)
		outputHID[17 + bluetooth]; // strength of effect at pressed state (requires supplement modes 4 and 20)
		outputHID[20 + bluetooth]; // effect actuation frequency in Hz (requires supplement modes 4 and 20)

		outputHID[22 + bluetooth]; //Mode Motor Left
		outputHID[23 + bluetooth]; //Left trigger start of resistance section
		outputHID[24 + bluetooth]; //Left trigger (mode1) amount of force exerted (mode2) end of resistance section supplemental mode 4+20) flag(s?) 0x02 = do not pause effect when fully presse
		outputHID[25 + bluetooth]; //Left trigger force exerted in range (mode2)
		outputHID[26 + bluetooth]; // strength of effect near release state (requires supplement modes 4 and 20)
		outputHID[27 + bluetooth]; // strength of effect near middle (requires supplement modes 4 and 20)
		outputHID[28 + bluetooth]; // strength of effect at pressed state (requires supplement modes 4 and 20)
		outputHID[31 + bluetooth]; // effect actuation frequency in Hz (requires supplement modes 4 and 20)
		*/

static VOID CALLBACK getUpdatesFromVirualController(PVIGEM_CLIENT Client, PVIGEM_TARGET Target, UCHAR LargeMotor, UCHAR SmallMotor, UCHAR LedNumber, LPVOID UserData)
{
	ControllerBridge &bridge = *reinterpret_cast<ControllerBridge *>(UserData);
	bridge.rumble[0] = SmallMotor;
	bridge.rumble[1] = LargeMotor;
	bridge.ledNumber = LedNumber;
}

static void zeroOutputReport(ControllerBridge &bridge)
{
	uint8_t outputHID[BT_BUFFER_SIZE];
	bool isBluetooth = bridge.isActualControllerBluetooth;
	ZeroMemory(outputHID, isBluetooth ? BT_BUFFER_SIZE : USB_BUFFER_SIZE);
	outputHID[0 + isBluetooth] = 0x02;
	outputHID[1 + isBluetooth] = 0x03 | 0x04 | 0x08;
	outputHID[2 + isBluetooth] = 0x55;
	if (isBluetooth)
		add_crc_to_buffer(outputHID);
	WriteFile(bridge.actualControllerHandle, outputHID, isBluetooth ? sizeof(outputHID) : USB_BUFFER_SIZE, NULL, NULL);
}

static int initializeVirtualController(PVIGEM_TARGET &virtualController, VIGEM_ERROR &error, PVIGEM_CLIENT &client)
{
	if (client == nullptr)
		return -1;
	const auto retval = vigem_connect(client);
	if (!VIGEM_SUCCESS(retval))
		return -1;
	virtualController = vigem_target_x360_alloc();
	error = vigem_target_add(client, virtualController);
	return 0;
}

int main(int argc, char *argv[])
{
	PVIGEM_CLIENT client = vigem_alloc();
	ControllerBridge bridge{};
	if (client == NULL || initializeVirtualController(bridge.virtualController, bridge.error, client) != 0)
	{
		if (MessageBox(NULL, L"The app couldn't start, please install VigemBusDriver", L"Vigem bus", MB_YESNO | MB_TASKMODAL) == IDNO)
			return -1;
		ShellExecuteW(0, 0, L"https://github.com/nefarius/ViGEmBus/releases/tag/v1.22.0", 0, 0, SW_SHOW);
		return -1;
	}

	ShellExecuteW(0, 0, L"C:\\Windows\\System32\\joy.cpl", 0, 0, SW_SHOW);

	vigem_target_x360_register_notification(client, bridge.virtualController, &getUpdatesFromVirualController, &bridge);

	uint64_t counter = 0;
	while (true)
	{
		++counter;
		if (bridge.actualControllerHandle)
		{
			getDualsenseInput(client, bridge, counter);
			sendDualsenseOutputReport(bridge, counter);
		}
		else if (counter % 0xFFFF == 1)
			isDualsenseConnected(bridge);
	}

	zeroOutputReport(bridge);
	vigem_target_remove(client, bridge.virtualController);
	vigem_target_free(bridge.virtualController);
	vigem_disconnect(client);
	vigem_free(client);
	return 0;
}
