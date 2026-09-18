#include "controllerIO.h"
#include <tlhelp32.h>
#include "User Settings/Adaptive Triggers/Adaptive Triggers.h"
#include "User Settings/Game Profiles/gameProfile.h"
#include <tchar.h>
#include "thread"

static bool isDualsenseConnected(controller& x360Controller) {

	x360Controller.deviceHandle = hid_open(SONY_VENDOR_ID, DUALSENSE_PRODUCT_ID, NULL);

	if (x360Controller.deviceHandle == nullptr) {
		printf("%ls\n", hid_error(x360Controller.deviceHandle));
		return false;
	}

	x360Controller.hidOffset = hid_get_device_info(x360Controller.deviceHandle)->interface_number == -1;
	x360Controller.isConnected = true;

	if (x360Controller.hidOffset) { //Bluetooth
		x360Controller.bufferSize = 78;
		x360Controller.inputBuffer[0] = 0x31; //Data report code
		return true;
	}
	//USB

	//disconnectBluetooth(x360Controller.deviceHandle, serialAddress);

	x360Controller.bufferSize = 64;
	x360Controller.inputBuffer[0] = 0x01; //Data report code
	return true;

}

static bool isDualsenseEdgeConnected(controller& x360Controller) {

	x360Controller.deviceHandle = hid_open(SONY_VENDOR_ID, DUALSENSEEDGE_PRODUCT_ID, NULL);

	if (x360Controller.deviceHandle == nullptr) {
		printf("%ls\n", hid_error(x360Controller.deviceHandle));
		return false;
	}

	x360Controller.hidOffset = hid_get_device_info(x360Controller.deviceHandle)->interface_number == -1;

	x360Controller.isConnected = true;

	if (x360Controller.hidOffset) { //Bluetooth
		x360Controller.bufferSize = 78;
		x360Controller.inputBuffer[0] = 0x31; //Data report code
		return true;
	}
	//USB
	x360Controller.bufferSize = 64;
	x360Controller.inputBuffer[0] = 0x01; //Data report code
	return true;
}

bool inline isControllerConnected(controller& x360Controller) {
	x360Controller.isConnected = false;

	//Stop output thread
	extern LPVOID asyncThreadPointer;
	if (reinterpret_cast<std::thread*>(asyncThreadPointer) != nullptr) {
		delete asyncThreadPointer;
		asyncThreadPointer = nullptr;
	}

	while (true) {
		Sleep(50); //Sleeps for 50ms so it doesnt spam the cpu

		extern void (*getInputs)(controller & x360Controller);

		if (isDualsenseConnected(x360Controller) || isDualsenseEdgeConnected(x360Controller)) {
			x360Controller.threadStop = false;
			getInputs = &getDualsenseInput;
			asyncThreadPointer = new std::thread(sendDualsenseOutputReport, std::ref(x360Controller));
			reinterpret_cast<std::thread*>(asyncThreadPointer)->detach();
			return true;
		}
	}

}

uint32_t computeCRC32(unsigned char* buffer, const size_t& len)
{
	UINT32 result = crcSeed;
	for (size_t i = 0; i < len; i++)
		// Compute crc
		result = hashTable[((unsigned char)result) ^ ((unsigned char)buffer[i])] ^ (result >> 8);
	// Return result
	return result;
}

unsigned char outputHID[547]{};
constexpr DWORD TITLE_SIZE = 1024;
int emulator{};
extern bool gameProfileSet;

#define isSelectPressed (buttonMapping[11])*(!(x360Controller.inputBuffer[33 + x360Controller.hidOffset] & (1 << 7)) & ((((x360Controller.inputBuffer[35 + x360Controller.hidOffset] & 0x0F) << 8) | (x360Controller.inputBuffer[34 + x360Controller.hidOffset])) <  800)) ^ (x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 4))
#define isStartPressed  (buttonMapping[11])*(!(x360Controller.inputBuffer[33 + x360Controller.hidOffset] & (1 << 7)) & ((((x360Controller.inputBuffer[35 + x360Controller.hidOffset] & 0x0F) << 8) | (x360Controller.inputBuffer[34 + x360Controller.hidOffset])) >= 800)) ^ (x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 5))

BOOL inline static CALLBACK FindWindowBySubstr(HWND hwnd, LPARAM substring) {
	TCHAR windowTitle[TITLE_SIZE];

	if (GetWindowText(hwnd, windowTitle, TITLE_SIZE)) {
		if (_tcsstr(windowTitle, LPCTSTR(substring)) != NULL)
			return true;
	}

	return false;
}

void extern inline sendDualsenseOutputReport(controller& x360Controller) {
	extern bool profileOpen;
	extern bool lightbarOpen;
	extern bool profileEdit;
	extern bool rumbleEnabled;
	extern int buttonMapping[20];
	extern UCHAR profileRumble;

	while (true) {

		Sleep(4);

		ZeroMemory(outputHID, 547);

		//USB Report ID or BT additional Flag
		outputHID[0 + x360Controller.hidOffset] = 0x02;

		//Trigger Flags
		outputHID[1 + x360Controller.hidOffset] = 0x03 | 0x04 | 0x08;
		outputHID[2 + x360Controller.hidOffset] = 0x55;

		outputHID[3 + x360Controller.hidOffset] = rumble[0] * rumbleEnabled + buttonMapping[19] * profileRumble; //Low Rumble
		outputHID[4 + x360Controller.hidOffset] = rumble[1] * rumbleEnabled + buttonMapping[19] * profileRumble; //High Rumble

		switch (x360Controller.batteryLevel + lightbarOpen + gameProfileSet + profileEdit) { //Chooses Lightbar Profile, if any of those editors are open then it breaks and sets itself to the currently being edited profile
		case 0: x360Controller.RGB[0].Index = 1; break;
		case 12: x360Controller.RGB[0].Index = 2; break;
		case 25: x360Controller.RGB[0].Index = 3; break;
		case 37: x360Controller.RGB[0].Index = 4; break;
		case 50: x360Controller.RGB[0].Index = 5; break;
		case 62: x360Controller.RGB[0].Index = 6; break;
		case 75: x360Controller.RGB[0].Index = 7; break;
		case 87: x360Controller.RGB[0].Index = 8; break;
		case 100: x360Controller.RGB[0].Index = 9; break;
		default: break;
		}

	LightEditorOpened:

		outputHID[9 + x360Controller.hidOffset] = x360Controller.RGB[x360Controller.RGB[0].Index].microhponeLed;
		outputHID[39 + x360Controller.hidOffset] = 0x02;
		outputHID[42 + x360Controller.hidOffset] = 0x02;
		outputHID[43 + x360Controller.hidOffset] = 0x02;


		for (int i = 0; i < 3; i++) {
			outputHID[45 + x360Controller.hidOffset + i] = x360Controller.RGB[x360Controller.RGB[0].Index].colors[i] * 255;
			x360Controller.RGB[0].colors[i] = x360Controller.RGB[x360Controller.RGB[0].Index].colors[i];
		}

		//Send Output Report
		if (x360Controller.threadStop) return;

		if (x360Controller.hidOffset) {
			outputHID[0] = 0x31; // BT Report ID

			const UINT32 crc = computeCRC32(outputHID, 74);

			outputHID[74] = (crc & 0x000000FF);
			outputHID[75] = ((crc & 0x0000FF00) >> 8UL);
			outputHID[76] = ((crc & 0x00FF0000) >> 16UL);
			outputHID[77] = ((crc & 0xFF000000) >> 24UL);

			hid_write(x360Controller.deviceHandle, outputHID, 547);
			continue;
		}
		//USB
		hid_write(x360Controller.deviceHandle, outputHID, 64);

	}

}

void inline static setButtons(controller& x360Controller) {

	// Normal Order
	x360Controller.ControllerState.Gamepad.wButtons = (bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 4)) ? XINPUT_GAMEPAD_X : 0; //Square

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 5)) ? XINPUT_GAMEPAD_A : 0; //Cross

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 6)) ? XINPUT_GAMEPAD_B : 0; //Circle

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 7)) ? XINPUT_GAMEPAD_Y : 0; //Triangle

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 0)) ? XINPUT_GAMEPAD_LEFT_SHOULDER : 0; //Left Shoulder

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 1)) ? XINPUT_GAMEPAD_RIGHT_SHOULDER : 0; //Right Shoulder

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 4)) ? XINPUT_GAMEPAD_BACK : 0; //Select

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 5)) ? XINPUT_GAMEPAD_START : 0; //Start

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 6)) ? XINPUT_GAMEPAD_LEFT_THUMB : 0; //Left Thumb

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 7)) ? XINPUT_GAMEPAD_RIGHT_THUMB : 0; //Right thumb

	switch ((int)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & 0x0f)) {
	case 0: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP; break;

	case 1: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP + XINPUT_GAMEPAD_DPAD_RIGHT; break;

	case 2: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT; break;

	case 3: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN + XINPUT_GAMEPAD_DPAD_RIGHT; break;

	case 4: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN; break;

	case 5: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN + XINPUT_GAMEPAD_DPAD_LEFT; break;

	case 6: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT; break;

	case 7: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP + XINPUT_GAMEPAD_DPAD_LEFT; break;
	}
}

void inline static setButtonsGameProfile(controller& x360Controller) {

	extern int buttonMapping[20];

	// Normal Order
	x360Controller.ControllerState.Gamepad.wButtons = (bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 4)) ? buttonMapping[0] : 0; //Square

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 5)) ? buttonMapping[1] : 0; //Cross

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 6)) ? buttonMapping[2] : 0; //Circle

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 7)) ? buttonMapping[3] : 0; //Triangle

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 0)) ? buttonMapping[4] : 0; //Left Shoulder

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 1)) ? buttonMapping[5] : 0; //Right Shoulder

	x360Controller.ControllerState.Gamepad.wButtons |= isSelectPressed ? buttonMapping[6] : 0;

	x360Controller.ControllerState.Gamepad.wButtons |= isStartPressed ? buttonMapping[7] : 0;

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 6)) ? buttonMapping[8] : 0; //Left Thumb

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 7)) ? buttonMapping[9] : 0; //Right thumb

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[10 + x360Controller.hidOffset] & (1 << 1)) ? buttonMapping[12] : 0; //Touchpad Button

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[10 + x360Controller.hidOffset] & (1 << 0)) ? buttonMapping[13] : 0; //PS Button

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[10 + x360Controller.hidOffset] & (1 << 2)) ? buttonMapping[14] : 0; //Mic Button

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[10 + x360Controller.hidOffset] & (1 << 4)) ? buttonMapping[15] : 0; //Left Function

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[10 + x360Controller.hidOffset] & (1 << 5)) ? buttonMapping[16] : 0; //Right Function

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[10 + x360Controller.hidOffset] & (1 << 6)) ? buttonMapping[17] : 0; //Left Paddle

	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[10 + x360Controller.hidOffset] & (1 << 7)) ? buttonMapping[18] : 0; //Right Paddle

	if (buttonMapping[10] == 1) {

		switch ((int)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & 0x0f)) {

		case 0: x360Controller.ControllerState.Gamepad.sThumbLY = 32767; break; //Up

		case 1: x360Controller.ControllerState.Gamepad.sThumbLY = 29727; x360Controller.ControllerState.Gamepad.sThumbLX = 19403; break; //Up Right

		case 2: x360Controller.ControllerState.Gamepad.sThumbLX = 32767; break; //Right

		case 3: x360Controller.ControllerState.Gamepad.sThumbLY = -29727; x360Controller.ControllerState.Gamepad.sThumbLX = 19403; break; //Down Right

		case 4: x360Controller.ControllerState.Gamepad.sThumbLY = -32768; break; //Down

		case 5: x360Controller.ControllerState.Gamepad.sThumbLY = -29727; x360Controller.ControllerState.Gamepad.sThumbLX = -22223; break; //Down Left

		case 6: x360Controller.ControllerState.Gamepad.sThumbLX = -32768; break; //Left

		case 7: x360Controller.ControllerState.Gamepad.sThumbLY = 29727; x360Controller.ControllerState.Gamepad.sThumbLX = -22223; break; //Up Left
		}
		return;
	}

	switch ((int)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & 0x0f)) {
	case 0: x360Controller.ControllerState.Gamepad.wButtons += XINPUT_GAMEPAD_DPAD_UP; break;

	case 1: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP + XINPUT_GAMEPAD_DPAD_RIGHT; break;

	case 2: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT; break;

	case 3: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN + XINPUT_GAMEPAD_DPAD_RIGHT; break;

	case 4: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN; break;

	case 5: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN + XINPUT_GAMEPAD_DPAD_LEFT; break;

	case 6: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT; break;

	case 7: x360Controller.ControllerState.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP + XINPUT_GAMEPAD_DPAD_LEFT; break;
	}

}

void inline getDualsenseInput(controller& x360Controller) {

	//bool readSuccess = ReadFile(x360Controller.deviceHandle, x360Controller.inputBuffer, x360Controller.bufferSize, NULL, NULL);
	
	if (hid_read(x360Controller.deviceHandle, x360Controller.inputBuffer, x360Controller.bufferSize) == -1) {
		x360Controller.threadStop = true;
		printf("%ls\n", hid_error(x360Controller.deviceHandle));
		hid_close(x360Controller.deviceHandle);
		isControllerConnected(x360Controller);
		return;
	}
	x360Controller.batteryLevel = (x360Controller.inputBuffer[53 + x360Controller.hidOffset] & 15) * 12.5; /* Hex 0x35 (USB) to get Battery / Hex 0x36 (Bluetooth) to get Battery
																											  because if bluetooth == true then bluetooth == 1 so we can just add bluetooth
																											  to the hex value of USB to get the battery reading
																										   */
	//Because of a bug on the Dualsense HID this needs to be implemented or else battery might display higher than 100 %
	x360Controller.batteryLevel = min(x360Controller.batteryLevel, 100);

	x360Controller.ControllerState.Gamepad.sThumbLX = ((x360Controller.inputBuffer[1 + x360Controller.hidOffset] * 257) - 32768);
	x360Controller.ControllerState.Gamepad.sThumbLY = (32767 - (x360Controller.inputBuffer[2 + x360Controller.hidOffset] * 257));
	x360Controller.ControllerState.Gamepad.sThumbRX = ((x360Controller.inputBuffer[3 + x360Controller.hidOffset] * 257) - 32768);
	x360Controller.ControllerState.Gamepad.sThumbRY = (32767 - (x360Controller.inputBuffer[4 + x360Controller.hidOffset] * 257));

	x360Controller.ControllerState.Gamepad.bLeftTrigger = x360Controller.inputBuffer[5 + x360Controller.hidOffset] * (x360Controller.shortTriggers == 0) + (((x360Controller.inputBuffer[5 + x360Controller.hidOffset]) >> 2) + 190) * (x360Controller.shortTriggers != 0);
	x360Controller.ControllerState.Gamepad.bRightTrigger = x360Controller.inputBuffer[6 + x360Controller.hidOffset] * (x360Controller.shortTriggers == 0) + (((x360Controller.inputBuffer[6 + x360Controller.hidOffset]) >> 2) + 190) * (x360Controller.shortTriggers != 0);

	if (gameProfileSet) {
		setButtonsGameProfile(x360Controller);
		return;
	}

	setButtons(x360Controller);

}
