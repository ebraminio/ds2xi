#include <windows.h>
#include <unordered_set>
#include <cstdio>
#include <cstdint>

#include "hidapi.h"
#include "ViGEm/Client.h"
#include "crc32.h"

// A connection between actual DualSense controller and virtual XInput device, created per matched controller
class Bridge
{
	// Actual controller's handle and connection type
	hid_device *device;
	bool isBluetooth;

	// Things to get from the virtual controller and pass to the actual one
	uint8_t smallMotor;
	uint8_t largeMotor;
	uint8_t ledNumber;

	// Controller state variables
	int batteryLevel;
	float redValue = 210, greenValue = 0, blueValue = 90;
	int redDirection = 1, greenDirection = 1, blueDirection = 1;

	// Things to pass to the actual controller
	bool microphoneLed;

	// Virtual controller handle
	PVIGEM_TARGET virtualController;
	VIGEM_ERROR error;

	// ViGEm client handle
	PVIGEM_CLIENT vigemClient;

	static constexpr bool demo = true;
	static constexpr unsigned USB_BUFFER_SIZE = 64;
	static constexpr unsigned BT_PAYLOAD_BUFFER_SIZE = 74;
	static constexpr unsigned BT_BUFFER_SIZE = 547;
	static constexpr unsigned BT_REPORT_ID = 0x31;

	static void add_crc_to_buffer(uint8_t *buffer)
	{
		buffer[0] = BT_REPORT_ID;
		const uint32_t crc = computeCRC32(buffer, BT_PAYLOAD_BUFFER_SIZE);
		buffer[BT_PAYLOAD_BUFFER_SIZE] = crc & 0x000000FF;
		buffer[BT_PAYLOAD_BUFFER_SIZE + 1] = (crc & 0x0000FF00) >> 8UL;
		buffer[BT_PAYLOAD_BUFFER_SIZE + 2] = (crc & 0x00FF0000) >> 16UL;
		buffer[BT_PAYLOAD_BUFFER_SIZE + 3] = (crc & 0xFF000000) >> 24UL;
	}

	bool sendDualsenseOutputReport(uint64_t counter)
	{
		uint8_t buffer[BT_BUFFER_SIZE];
		ZeroMemory(buffer, isBluetooth ? BT_BUFFER_SIZE : USB_BUFFER_SIZE);

		// USB Report ID or BT additional Flag
		buffer[0 + isBluetooth] = 0x02;

		// Trigger Flags
		buffer[1 + isBluetooth] = 0x03 | 0x04 | 0x08;
		buffer[2 + isBluetooth] = 0x55;

		buffer[3 + isBluetooth] = smallMotor; // Low Rumble
		buffer[4 + isBluetooth] = largeMotor; // High Rumble
		buffer[9 + isBluetooth] = microphoneLed;

		buffer[39 + isBluetooth] = 0x02;
		buffer[42 + isBluetooth] = 0x02;
		buffer[43 + isBluetooth] = 0x02;

		buffer[44 + isBluetooth] = ledNumber;
		if (demo && ledNumber == 0)
			buffer[44 + isBluetooth] = (counter / 1000) % 2 ? 0b00100 : 0b10001;

		if (demo)
		{
			if (counter % 0xF == 0)
			{
				if (redValue == 255)
					redDirection = -1;
				if (redValue == 0)
					redDirection = 1;
				if (greenValue == 255)
					greenDirection = -1;
				if (greenValue == 0)
					greenDirection = 1;
				if (blueValue == 255)
					blueDirection = -1;
				if (blueValue == 0)
					blueDirection = 1;
				redValue += 2.5f * redDirection;
				greenValue += 2.5f * greenDirection;
				blueValue += 2.5f * blueDirection;
			}
			buffer[45 + isBluetooth] = redValue;
			buffer[46 + isBluetooth] = greenValue;
			buffer[47 + isBluetooth] = blueValue;
		}
		if (isBluetooth)
			add_crc_to_buffer(buffer);
		hid_write(device, buffer, isBluetooth ? BT_BUFFER_SIZE : USB_BUFFER_SIZE);
		return true;
	}

	void getDualsenseInput(uint64_t counter)
	{
		uint8_t buffer[574];
		int bufferSize = hid_read(device, buffer, sizeof(buffer));
		if (bufferSize == 0)
			// Non blocking read can return 0 data
			return;
		else if (bufferSize == -1)
		{
			printf("%ls\n", hid_read_error(device));
			return;
		}
		else if (bufferSize < 54)
		{
			printf("Buffer size is too small: %d\n", bufferSize);
			return;
		}

		// Because of a bug on the Dualsense HID this needs to be implemented or else battery might display higher than 100 %
		batteryLevel = min((buffer[53 + isBluetooth] & 15) * 12.5, 100);

		XUSB_REPORT gamepadReport{};
		gamepadReport.sThumbLX = (buffer[1 + isBluetooth] * 257) - 32768;
		gamepadReport.sThumbLY = 32767 - (buffer[2 + isBluetooth] * 257);
		gamepadReport.sThumbRX = (buffer[3 + isBluetooth] * 257) - 32768;
		gamepadReport.sThumbRY = 32767 - (buffer[4 + isBluetooth] * 257);

		gamepadReport.bLeftTrigger = buffer[5 + isBluetooth];
		gamepadReport.bRightTrigger = buffer[6 + isBluetooth];

		// Normal Order
		gamepadReport.wButtons = (bool)(buffer[8 + isBluetooth] & (1 << 4)) ? XUSB_GAMEPAD_X : 0;				// Square
		gamepadReport.wButtons |= (bool)(buffer[8 + isBluetooth] & (1 << 5)) ? XUSB_GAMEPAD_A : 0;				// Cross
		gamepadReport.wButtons |= (bool)(buffer[8 + isBluetooth] & (1 << 6)) ? XUSB_GAMEPAD_B : 0;				// Circle
		gamepadReport.wButtons |= (bool)(buffer[8 + isBluetooth] & (1 << 7)) ? XUSB_GAMEPAD_Y : 0;				// Triangle
		gamepadReport.wButtons |= (bool)(buffer[9 + isBluetooth] & (1 << 0)) ? XUSB_GAMEPAD_LEFT_SHOULDER : 0;	// Left Shoulder
		gamepadReport.wButtons |= (bool)(buffer[9 + isBluetooth] & (1 << 1)) ? XUSB_GAMEPAD_RIGHT_SHOULDER : 0; // Right Shoulder
		gamepadReport.wButtons |= (bool)(buffer[9 + isBluetooth] & (1 << 4)) ? XUSB_GAMEPAD_BACK : 0;			// Select
		gamepadReport.wButtons |= (bool)(buffer[9 + isBluetooth] & (1 << 5)) ? XUSB_GAMEPAD_START : 0;			// Start
		gamepadReport.wButtons |= (bool)(buffer[9 + isBluetooth] & (1 << 6)) ? XUSB_GAMEPAD_LEFT_THUMB : 0;		// Left Thumb
		gamepadReport.wButtons |= (bool)(buffer[9 + isBluetooth] & (1 << 7)) ? XUSB_GAMEPAD_RIGHT_THUMB : 0;	// Right thumb

		// XUSB_GAMEPAD_GUIDE is undocumented on XInput, but it is used by the Xbox button on the controller. The DualSense controller has a similar button, which is mapped to the GUIDE button in this code.
		gamepadReport.wButtons |= (bool)(buffer[10 + isBluetooth] & (1 << 0)) ? XUSB_GAMEPAD_GUIDE : 0;
		// 1 << 1 => Touchpad Button
		// 1 << 2 => Mic Button
		// DualSense Edge:
		//  1 << 4 => Left Function
		//  1 << 5 => Right Function
		//  1 << 6 => Left Paddle
		//  1 << 7 => Right Paddle
		if (demo)
			// Turn on the microphone LED if either the touchpad button or the mic button is pressed
			microphoneLed = buffer[10 + isBluetooth] & (1 << 1) || buffer[10 + isBluetooth] & (1 << 2);

		uint8_t dpad = buffer[8 + isBluetooth] & 0x0f;
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

		vigem_target_x360_update(vigemClient, virtualController, gamepadReport);
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
		auto &bridge = *reinterpret_cast<Bridge *>(UserData);
		bridge.smallMotor = SmallMotor;
		bridge.largeMotor = LargeMotor;
		bridge.ledNumber = LedNumber;
	}

	void zeroOutputReport()
	{
		uint8_t outputHID[BT_BUFFER_SIZE];
		ZeroMemory(outputHID, isBluetooth ? BT_BUFFER_SIZE : USB_BUFFER_SIZE);
		outputHID[0 + isBluetooth] = 0x02;
		outputHID[1 + isBluetooth] = 0x03 | 0x04 | 0x08;
		outputHID[2 + isBluetooth] = 0x55;
		if (isBluetooth)
			add_crc_to_buffer(outputHID);
		hid_write(device, outputHID, isBluetooth ? sizeof(outputHID) : USB_BUFFER_SIZE);
	}

public:
	void sync(uint64_t counter)
	{
		getDualsenseInput(counter);
		sendDualsenseOutputReport(counter);
	}

	bool matches(hid_device_info *deviceInfo)
	{
		return wcscmp(hid_get_device_info(device)->serial_number, deviceInfo->serial_number) == 0;
	}

	Bridge(PVIGEM_CLIENT vigemClient, hid_device_info *deviceInfo)
	{
		this->vigemClient = vigemClient;
		this->isBluetooth = deviceInfo->interface_number == -1;

		device = hid_open(deviceInfo->vendor_id, deviceInfo->product_id, deviceInfo->serial_number);
		if (device == nullptr)
			printf("%ls\n", hid_error(device));

		// As we want to support multiple controllers, we set the device to non-blocking mode
		hid_set_nonblocking(device, true);

		this->virtualController = vigem_target_x360_alloc();
		if (!VIGEM_SUCCESS(vigem_target_add(vigemClient, virtualController)))
			printf("Failed to add virtual controller: %ls\n", hid_error(device));
		vigem_target_x360_register_notification(vigemClient, virtualController, &getUpdatesFromVirualController, this);
	}

	~Bridge()
	{
		zeroOutputReport();
		hid_close(device);
		vigem_target_remove(vigemClient, virtualController);
		vigem_target_free(virtualController);
	}
};

class BridgeManager
{
	BridgeManager(const BridgeManager &) = delete;
	BridgeManager &operator=(const BridgeManager &) = delete;
	std::unordered_set<Bridge *> bridges{};
	PVIGEM_CLIENT vigemClient = vigem_alloc();
	hid_hotplug_callback_handle hotplugHandle = 0;
	hid_hotplug_callback_handle hotplugEdgeHandle = 0;
	uint64_t counter = 0;

	static int hotplugCallback(
		hid_hotplug_callback_handle callback_handle,
		struct hid_device_info *device,
		hid_hotplug_event event,
		void *user_data)
	{
		BridgeManager &manager = *reinterpret_cast<BridgeManager *>(user_data);
		if (event == HID_API_HOTPLUG_EVENT_DEVICE_ARRIVED)
			manager.add(device);
		else if (event == HID_API_HOTPLUG_EVENT_DEVICE_LEFT)
			manager.remove(device);
		return 0;
	}

	void add(hid_device_info *deviceInfo)
	{
		bridges.insert(new Bridge(vigemClient, deviceInfo));
	}

	void remove(hid_device_info *deviceInfo)
	{
		for (auto *bridge : bridges)
			if (bridge->matches(deviceInfo))
			{
				bridges.erase(bridge);
				delete bridge;
				break;
			}
	}

	static constexpr int SONY_VENDOR_ID = 0x054c;

	static constexpr int DUALSENSE_PRODUCT_ID = 0x0ce6;
	static constexpr int DUALSENSEEDGE_PRODUCT_ID = 0x0df2;
	static constexpr int DUALSHOCK4_PRODUCT_ID = 0x09cc;

public:
	BridgeManager()
	{
		if (vigemClient == nullptr || !VIGEM_SUCCESS(vigem_connect(vigemClient)))
		{
			if (MessageBoxW(NULL, L"The app couldn't start, please install VigemBusDriver", L"Vigem bus", MB_YESNO | MB_TASKMODAL) != IDNO)
				ShellExecuteW(0, 0, L"https://github.com/nefarius/ViGEmBus/releases/tag/v1.22.0", 0, 0, SW_SHOW);
			exit(-1);
		}

		if (
			hid_hotplug_register_callback(SONY_VENDOR_ID, DUALSENSE_PRODUCT_ID, HID_API_HOTPLUG_EVENT_DEVICE_ARRIVED | HID_API_HOTPLUG_EVENT_DEVICE_LEFT, HID_API_HOTPLUG_ENUMERATE, hotplugCallback, this, &hotplugHandle) != 0 ||
			hid_hotplug_register_callback(SONY_VENDOR_ID, DUALSENSEEDGE_PRODUCT_ID, HID_API_HOTPLUG_EVENT_DEVICE_ARRIVED | HID_API_HOTPLUG_EVENT_DEVICE_LEFT, HID_API_HOTPLUG_ENUMERATE, hotplugCallback, this, &hotplugEdgeHandle) != 0)
		{
			printf("Failed to register hotplug callback\n");
			exit(-1);
		}
	}

	void sync()
	{
		if (bridges.empty())
			Sleep(4);
		else
		{
			++counter;
			for (auto *bridge : bridges)
			{
				bridge->sync(counter);
			}
		}
	}

	~BridgeManager()
	{
		hid_hotplug_deregister_callback(hotplugHandle);
		hid_hotplug_deregister_callback(hotplugEdgeHandle);
		if (vigemClient != nullptr)
		{
			vigem_disconnect(vigemClient);
			vigem_free(vigemClient);
		}
	}
};

static constexpr unsigned notifyClickId = WM_USER + 1;

static LRESULT CALLBACK trayWindowProcedure(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(ERROR_SUCCESS);
		return 0;

	case notifyClickId:
		if (lParam == WM_RBUTTONUP)
			PostQuitMessage(ERROR_SUCCESS);
		else if (lParam == WM_LBUTTONUP)
			ShellExecuteW(0, 0, L"C:\\Windows\\System32\\joy.cpl", 0, 0, SW_SHOW);
		return 0;

	default:
		break;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

constexpr auto appId = L"ds2xi";

int main(int argc, char *argv[])
{
	HANDLE mutex = CreateMutexW(nullptr, 0, appId);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		printf("Another instance of the app is already running.\n");
		return -1;
	}

	BridgeManager bridgeManager{};

	HINSTANCE hInst = GetModuleHandleW(nullptr);
	{
		WNDCLASSEXW wc{};
		wc.hInstance = hInst;
		wc.cbSize = sizeof(WNDCLASSEXW);
		wc.lpfnWndProc = trayWindowProcedure;
		wc.lpszClassName = appId;
		RegisterClassExW(&wc);
	}

	HWND hWnd = CreateWindowExW(0, appId, nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);

	NOTIFYICONDATAW notifyIconData{};

	HICON hIcon = nullptr;
	ExtractIconExW(L"joy.cpl", 0, &hIcon, nullptr, 1);
	notifyIconData.hIcon = hIcon;
	notifyIconData.cbSize = sizeof(NOTIFYICONDATAW);
	notifyIconData.uCallbackMessage = notifyClickId;
	notifyIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	notifyIconData.hWnd = hWnd;
	wcscpy_s(notifyIconData.szTip, L"DualSense to XInput");
	Shell_NotifyIconW(NIM_ADD, &notifyIconData);

	MSG msg;
	while (true)
	{
		if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		bridgeManager.sync();
	}

	Shell_NotifyIconW(NIM_DELETE, &notifyIconData);
	DestroyIcon(notifyIconData.hIcon);

	return 0;
}
