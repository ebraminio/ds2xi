#ifdef NDEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif
#include <windows.h>
#include <Xinput.h>
#include "ViGEm/Client.h"
#include <hidapi.h>
#include <thread>


constexpr int SONY_VENDOR_ID = 0x054c;

constexpr int DUALSENSE_PRODUCT_ID = 0x0ce6;
constexpr int DUALSENSEEDGE_PRODUCT_ID = 0x0df2;
constexpr int DUALSHOCK4_PRODUCT_ID = 0x09CC;

struct RGB {
	float colors[3]{}; //Red Green Blue
	short int microhponeLed;
	int Index;
};

struct controller {
	unsigned char inputBuffer[574]{};
	bool hidOffset;
	bool isConnected{ false };
	bool threadStop{ false };

	PVIGEM_CLIENT client;
	PVIGEM_TARGET emulateX360;
	XINPUT_STATE ControllerState;
	int batteryLevel;

	int bufferSize;
	int shortTriggers{};
	RGB RGB[10]{};

	VIGEM_ERROR target;
	hid_device* deviceHandle{ nullptr };
} *ptrController = nullptr;
std::thread *asyncThreadPointer = nullptr;
UCHAR rumble[2]{};
unsigned char outputHID[547]{};
constexpr DWORD TITLE_SIZE = 1024;
bool profileOpen;
bool lightbarOpen;
bool profileEdit;
bool rumbleEnabled;

const UINT32 crcSeed = 0xeada2d49;

const uint32_t hashTable[256] = {
	0xd202ef8d, 0xa505df1b, 0x3c0c8ea1, 0x4b0bbe37, 0xd56f2b94, 0xa2681b02, 0x3b614ab8, 0x4c667a2e,
	0xdcd967bf, 0xabde5729, 0x32d70693, 0x45d03605, 0xdbb4a3a6, 0xacb39330, 0x35bac28a, 0x42bdf21c,
	0xcfb5ffe9, 0xb8b2cf7f, 0x21bb9ec5, 0x56bcae53, 0xc8d83bf0, 0xbfdf0b66, 0x26d65adc, 0x51d16a4a,
	0xc16e77db, 0xb669474d, 0x2f6016f7, 0x58672661, 0xc603b3c2, 0xb1048354, 0x280dd2ee, 0x5f0ae278,
	0xe96ccf45, 0x9e6bffd3, 0x762ae69, 0x70659eff, 0xee010b5c, 0x99063bca, 0xf6a70, 0x77085ae6,
	0xe7b74777, 0x90b077e1, 0x9b9265b, 0x7ebe16cd, 0xe0da836e, 0x97ddb3f8, 0xed4e242, 0x79d3d2d4,
	0xf4dbdf21, 0x83dcefb7, 0x1ad5be0d, 0x6dd28e9b, 0xf3b61b38, 0x84b12bae, 0x1db87a14, 0x6abf4a82,
	0xfa005713, 0x8d076785, 0x140e363f, 0x630906a9, 0xfd6d930a, 0x8a6aa39c, 0x1363f226, 0x6464c2b0,
	0xa4deae1d, 0xd3d99e8b, 0x4ad0cf31, 0x3dd7ffa7, 0xa3b36a04, 0xd4b45a92, 0x4dbd0b28, 0x3aba3bbe,
	0xaa05262f, 0xdd0216b9, 0x440b4703, 0x330c7795, 0xad68e236, 0xda6fd2a0, 0x4366831a, 0x3461b38c,
	0xb969be79, 0xce6e8eef, 0x5767df55, 0x2060efc3, 0xbe047a60, 0xc9034af6, 0x500a1b4c, 0x270d2bda,
	0xb7b2364b, 0xc0b506dd, 0x59bc5767, 0x2ebb67f1, 0xb0dff252, 0xc7d8c2c4, 0x5ed1937e, 0x29d6a3e8,
	0x9fb08ed5, 0xe8b7be43, 0x71beeff9, 0x6b9df6f, 0x98dd4acc, 0xefda7a5a, 0x76d32be0, 0x1d41b76,
	0x916b06e7, 0xe66c3671, 0x7f6567cb, 0x862575d, 0x9606c2fe, 0xe101f268, 0x7808a3d2, 0xf0f9344,
	0x82079eb1, 0xf500ae27, 0x6c09ff9d, 0x1b0ecf0b, 0x856a5aa8, 0xf26d6a3e, 0x6b643b84, 0x1c630b12,
	0x8cdc1683, 0xfbdb2615, 0x62d277af, 0x15d54739, 0x8bb1d29a, 0xfcb6e20c, 0x65bfb3b6, 0x12b88320,
	0x3fba6cad, 0x48bd5c3b, 0xd1b40d81, 0xa6b33d17, 0x38d7a8b4, 0x4fd09822, 0xd6d9c998, 0xa1def90e,
	0x3161e49f, 0x4666d409, 0xdf6f85b3, 0xa868b525, 0x360c2086, 0x410b1010, 0xd80241aa, 0xaf05713c,
	0x220d7cc9, 0x550a4c5f, 0xcc031de5, 0xbb042d73, 0x2560b8d0, 0x52678846, 0xcb6ed9fc, 0xbc69e96a,
	0x2cd6f4fb, 0x5bd1c46d, 0xc2d895d7, 0xb5dfa541, 0x2bbb30e2, 0x5cbc0074, 0xc5b551ce, 0xb2b26158,
	0x4d44c65, 0x73d37cf3, 0xeada2d49, 0x9ddd1ddf, 0x3b9887c, 0x74beb8ea, 0xedb7e950, 0x9ab0d9c6,
	0xa0fc457, 0x7d08f4c1, 0xe401a57b, 0x930695ed, 0xd62004e, 0x7a6530d8, 0xe36c6162, 0x946b51f4,
	0x19635c01, 0x6e646c97, 0xf76d3d2d, 0x806a0dbb, 0x1e0e9818, 0x6909a88e, 0xf000f934, 0x8707c9a2,
	0x17b8d433, 0x60bfe4a5, 0xf9b6b51f, 0x8eb18589, 0x10d5102a, 0x67d220bc, 0xfedb7106, 0x89dc4190,
	0x49662d3d, 0x3e611dab, 0xa7684c11, 0xd06f7c87, 0x4e0be924, 0x390cd9b2, 0xa0058808, 0xd702b89e,
	0x47bda50f, 0x30ba9599, 0xa9b3c423, 0xdeb4f4b5, 0x40d06116, 0x37d75180, 0xaede003a, 0xd9d930ac,
	0x54d13d59, 0x23d60dcf, 0xbadf5c75, 0xcdd86ce3, 0x53bcf940, 0x24bbc9d6, 0xbdb2986c, 0xcab5a8fa,
	0x5a0ab56b, 0x2d0d85fd, 0xb404d447, 0xc303e4d1, 0x5d677172, 0x2a6041e4, 0xb369105e, 0xc46e20c8,
	0x72080df5, 0x50f3d63, 0x9c066cd9, 0xeb015c4f, 0x7565c9ec, 0x262f97a, 0x9b6ba8c0, 0xec6c9856,
	0x7cd385c7, 0xbd4b551, 0x92dde4eb, 0xe5dad47d, 0x7bbe41de, 0xcb97148, 0x95b020f2, 0xe2b71064,
	0x6fbf1d91, 0x18b82d07, 0x81b17cbd, 0xf6b64c2b, 0x68d2d988, 0x1fd5e91e, 0x86dcb8a4, 0xf1db8832,
	0x616495a3, 0x1663a535, 0x8f6af48f, 0xf86dc419, 0x660951ba, 0x110e612c, 0x88073096, 0xff000000,
};

static bool isDualsenseConnected(controller& x360Controller) {
	x360Controller.deviceHandle = hid_open(SONY_VENDOR_ID, DUALSENSE_PRODUCT_ID, NULL);

	if (x360Controller.deviceHandle == nullptr) {
		x360Controller.deviceHandle = hid_open(SONY_VENDOR_ID, DUALSENSEEDGE_PRODUCT_ID, NULL);
		if (x360Controller.deviceHandle == nullptr) {
			printf("%ls\n", hid_error(x360Controller.deviceHandle));
			return false;
		}
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

uint32_t computeCRC32(unsigned char* buffer, const size_t& len)
{
	UINT32 result = crcSeed;
	for (size_t i = 0; i < len; i++)
		// Compute crc
		result = hashTable[((unsigned char)result) ^ ((unsigned char)buffer[i])] ^ (result >> 8);
	// Return result
	return result;
}

static void sendDualsenseOutputReport(controller& x360Controller) {
	while (true) {

		Sleep(4);

		ZeroMemory(outputHID, 547);

		//USB Report ID or BT additional Flag
		outputHID[0 + x360Controller.hidOffset] = 0x02;

		//Trigger Flags
		outputHID[1 + x360Controller.hidOffset] = 0x03 | 0x04 | 0x08;
		outputHID[2 + x360Controller.hidOffset] = 0x55;

		outputHID[3 + x360Controller.hidOffset] = rumble[0]; //Low Rumble
		outputHID[4 + x360Controller.hidOffset] = rumble[1]; //High Rumble

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

static bool isControllerConnected(controller& x360Controller) {
	x360Controller.isConnected = false;

	//Stop output thread
	if (asyncThreadPointer != nullptr) {
		delete asyncThreadPointer;
		asyncThreadPointer = nullptr;
	}

	while (true) {
		Sleep(50); //Sleeps for 50ms so it doesnt spam the cpu

		if (isDualsenseConnected(x360Controller)) {
			x360Controller.threadStop = false;
			asyncThreadPointer = new std::thread(sendDualsenseOutputReport, std::ref(x360Controller));
			asyncThreadPointer->detach();
			return true;
		}
	}
}

static void getDualsenseInput(controller& x360Controller) {

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

	// XUSB_GAMEPAD_GUIDE is undocumented on XInput, but it is used by the Xbox button on the controller. The DualSense controller has a similar button, which is mapped to the GUIDE button in this code.
	x360Controller.ControllerState.Gamepad.wButtons |= (bool)(x360Controller.inputBuffer[10 + x360Controller.hidOffset] & (1 << 0)) ? XUSB_GAMEPAD_GUIDE : 0;
	// 1 << 1 => touch screen button
	// 1 << 2 => mic button

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


VOID CALLBACK getRumble(PVIGEM_CLIENT Client, PVIGEM_TARGET Target, UCHAR LargeMotor, UCHAR SmallMotor, UCHAR LedNumber, LPVOID UserData) {
	rumble[0] = SmallMotor;
	rumble[1] = LargeMotor;
}

void zeroOutputReport() {
	unsigned char outputHID[547]{};
	if (ptrController->hidOffset) {
		ZeroMemory(outputHID, 547);

		outputHID[0] = 0x31;
		outputHID[1] = 0x02;
		outputHID[2] = 0x03 | 0x04 | 0x08;
		outputHID[3] = 0x55;

		const UINT32 crc = computeCRC32(outputHID, 74);

		outputHID[74] = (crc & 0x000000FF);
		outputHID[75] = ((crc & 0x0000FF00) >> 8UL);
		outputHID[76] = ((crc & 0x00FF0000) >> 16UL);
		outputHID[77] = ((crc & 0xFF000000) >> 24UL);

		WriteFile(ptrController->deviceHandle, outputHID, 547, NULL, NULL);
	}
	else {
		ZeroMemory(outputHID, 547);

		outputHID[0] = 0x02;
		outputHID[1] = 0x03 | 0x04 | 0x08;
		outputHID[2] = 0x55;

		WriteFile(ptrController->deviceHandle, outputHID, 64, NULL, NULL);
	}
}

BOOL WINAPI exitFunction(_In_ DWORD dwCtrlType) {
	if (asyncThreadPointer != nullptr) {
		ptrController->threadStop = true;
		delete asyncThreadPointer;
		asyncThreadPointer = nullptr;
	}
	zeroOutputReport();

	//Cleanup
	vigem_target_remove(ptrController->client, ptrController->emulateX360);
	vigem_target_free(ptrController->emulateX360);
	vigem_disconnect(ptrController->client);
	vigem_free(ptrController->client);
	_exit(NULL);
	return TRUE;
}

static int initializeFakeController(PVIGEM_TARGET& emulateX360, VIGEM_ERROR& target, PVIGEM_CLIENT& client) {

	if (client == nullptr) return -1;

	const auto retval = vigem_connect(client);

	if (!VIGEM_SUCCESS(retval)) return -1;

	emulateX360 = vigem_target_x360_alloc();

	target = vigem_target_add(client, emulateX360);

	return 0;
}

int main(int argc,char* argv[]) {
#ifdef NDEBUG
	autoUpdater();
#endif
	SetProcessShutdownParameters(2, 0);
	SetConsoleCtrlHandler(exitFunction, TRUE);

	//Initialize Fake Controller
	controller x360Controller{};

	x360Controller.client = vigem_alloc();

	if (x360Controller.client == NULL || initializeFakeController(x360Controller.emulateX360, x360Controller.target, x360Controller.client) != 0) {
		if (MessageBox(NULL, L"The app couldn't start, please install VigemBusDriver ,if this error persists please open an issue on github", L"Vigem bus", MB_YESNO | MB_TASKMODAL) == IDNO) return -1;
		ShellExecute(0, 0, L"https://github.com/nefarius/ViGEmBus/releases/tag/v1.22.0", 0, 0, SW_SHOW);
		return -1;
	}

	ptrController = &x360Controller;

	vigem_target_x360_register_notification(x360Controller.client, x360Controller.emulateX360, &getRumble, ptrController);
	isControllerConnected(x360Controller);
	while (true) {
		XInputGetState(0, &x360Controller.ControllerState);

		getDualsenseInput(x360Controller);

		vigem_target_x360_update(x360Controller.client, x360Controller.emulateX360, *reinterpret_cast<XUSB_REPORT*>(&x360Controller.ControllerState.Gamepad));
	}
	return 0;
}
