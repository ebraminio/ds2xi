#include <windows.h>
#include <unordered_set>
#include <cstdio>
#include <cstdint>
#include <memory>

#include "hidapi.h"
#include "ViGEm/Client.h"
#include "crc32.h"

// A connection between actual DualSense controller and virtual XInput device, created per matched controller
class Bridge
{
	// Actual controller's handle and connection type
	hid_device *device = nullptr;
	bool isBluetooth = false;

	// Misc internal state
	uint8_t smallMotor = 0;
	uint8_t largeMotor = 0;
	uint8_t ledNumber = 0;
	uint8_t redValue = 0;
	uint8_t greenValue = 0;
	uint8_t blueValue = 0xff;
	uint8_t batteryLevel = 0;

	// Virtual controller handle
	PVIGEM_TARGET virtualController = nullptr;
	VIGEM_ERROR error = VIGEM_ERROR_NONE;

	// ViGEm client handle
	PVIGEM_CLIENT vigemClient = nullptr;

	static constexpr unsigned USB_BUFFER_SIZE = 64;
	static constexpr unsigned BT_PAYLOAD_BUFFER_SIZE = 74;
	static constexpr unsigned BT_BUFFER_SIZE = 547;
	static constexpr unsigned BT_REPORT_ID = 0x31;

	void add_crc_to_buffer(uint8_t *buffer)
	{
		if (!isBluetooth)
			return;
		buffer[0] = BT_REPORT_ID;
		const uint32_t crc = computeCRC32(buffer, BT_PAYLOAD_BUFFER_SIZE);
		buffer[BT_PAYLOAD_BUFFER_SIZE + 0] = crc & 0x000000FF;
		buffer[BT_PAYLOAD_BUFFER_SIZE + 1] = (crc & 0x0000FF00) >> 8UL;
		buffer[BT_PAYLOAD_BUFFER_SIZE + 2] = (crc & 0x00FF0000) >> 16UL;
		buffer[BT_PAYLOAD_BUFFER_SIZE + 3] = (crc & 0xFF000000) >> 24UL;
	}

	void setDualSenseState()
	{
		uint8_t buffer[BT_BUFFER_SIZE];
		ZeroMemory(buffer, isBluetooth ? BT_BUFFER_SIZE : USB_BUFFER_SIZE);

		// USB Report ID or BT additional Flag
		buffer[0 + isBluetooth] = 0x02;

		// Trigger Flags
		buffer[1 + isBluetooth] = 0x03 | 0x04 | 0x08;
		buffer[2 + isBluetooth] = 0x55;

		buffer[3 + isBluetooth] = this->smallMotor; // Low Rumble
		buffer[4 + isBluetooth] = this->largeMotor; // High Rumble

		buffer[39 + isBluetooth] = 0x02;
		buffer[42 + isBluetooth] = 0x02;
		buffer[43 + isBluetooth] = 0x02;

		buffer[45 + isBluetooth] = this->redValue;
		buffer[46 + isBluetooth] = this->greenValue;
		buffer[47 + isBluetooth] = this->blueValue;

		if (this->ledNumber == 0)
		{
			static const uint8_t levels[] = {0b00000, 0b00001, 0b00011, 0b00111, 0b01111, 0b11111};
			const unsigned levelsCount = sizeof(levels) / sizeof(levels[0]);
			const unsigned levelIndex = min((this->batteryLevel / 100.f) * (levelsCount - 1), levelsCount - 1);
			buffer[44 + isBluetooth] = levels[levelIndex];
		}
		else
			buffer[44 + isBluetooth] = this->ledNumber;

		add_crc_to_buffer(buffer);
		hid_write(device, buffer, isBluetooth ? BT_BUFFER_SIZE : USB_BUFFER_SIZE);
	}

	void getDualSenseInput()
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
		else if (bufferSize < 55)
		{
			printf("Buffer size is too small: %d\n", bufferSize);
			return;
		}

		// Apparently can go higher than 100 due to a bug so let's cap it
		uint8_t newBatteryLevel = min((buffer[53 + isBluetooth] & 15) * 12.5, 100);
		if (batteryLevel != newBatteryLevel)
		{
			batteryLevel = newBatteryLevel;
			printf("new battery level: %d\n", batteryLevel);
			setDualSenseState();
		}

		XUSB_REPORT gamepadReport{};
		gamepadReport.sThumbLX = (buffer[1 + isBluetooth] * 257) - 32768;
		gamepadReport.sThumbLY = 32767 - (buffer[2 + isBluetooth] * 257);
		gamepadReport.sThumbRX = (buffer[3 + isBluetooth] * 257) - 32768;
		gamepadReport.sThumbRY = 32767 - (buffer[4 + isBluetooth] * 257);

		gamepadReport.bLeftTrigger = buffer[5 + isBluetooth];
		gamepadReport.bRightTrigger = buffer[6 + isBluetooth];

		if (buffer[8 + isBluetooth] & (1 << 4))
			gamepadReport.wButtons |= XUSB_GAMEPAD_X; // Square
		if (buffer[8 + isBluetooth] & (1 << 5))
			gamepadReport.wButtons |= XUSB_GAMEPAD_A; // Cross
		if (buffer[8 + isBluetooth] & (1 << 6))
			gamepadReport.wButtons |= XUSB_GAMEPAD_B; // Circle
		if (buffer[8 + isBluetooth] & (1 << 7))
			gamepadReport.wButtons |= XUSB_GAMEPAD_Y; // Triangle
		if (buffer[9 + isBluetooth] & (1 << 0))
			gamepadReport.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER; // Left Shoulder
		if (buffer[9 + isBluetooth] & (1 << 1))
			gamepadReport.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER; // Right Shoulder
		if (buffer[9 + isBluetooth] & (1 << 4))
			gamepadReport.wButtons |= XUSB_GAMEPAD_BACK; // Select
		if (buffer[9 + isBluetooth] & (1 << 5))
			gamepadReport.wButtons |= XUSB_GAMEPAD_START; // Start
		if (buffer[9 + isBluetooth] & (1 << 6))
			gamepadReport.wButtons |= XUSB_GAMEPAD_LEFT_THUMB; // Left Thumb
		if (buffer[9 + isBluetooth] & (1 << 7))
			gamepadReport.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB; // Right Thumb
		if (buffer[10 + isBluetooth] & (1 << 0))
			gamepadReport.wButtons |= XUSB_GAMEPAD_GUIDE; // PS Button
		// Other useful bits on 10th bytes,
		// 1 << 1 => Touchpad Button
		// 1 << 2 => Mic Button
		// DualSense Edge:
		//  1 << 4 => Left Function
		//  1 << 5 => Right Function
		//  1 << 6 => Left Paddle
		//  1 << 7 => Right Paddle

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

	static VOID CALLBACK getUpdatesFromVirualController(PVIGEM_CLIENT Client, PVIGEM_TARGET Target, UCHAR LargeMotor, UCHAR SmallMotor, UCHAR LedNumber, LPVOID UserData)
	{
		auto &bridge = *reinterpret_cast<Bridge *>(UserData);
		bridge.smallMotor = SmallMotor;
		bridge.largeMotor = LargeMotor;
		bridge.ledNumber = LedNumber;
		bridge.setDualSenseState();
	}

	void cleanControllerState()
	{
		uint8_t outputHID[BT_BUFFER_SIZE];
		ZeroMemory(outputHID, isBluetooth ? BT_BUFFER_SIZE : USB_BUFFER_SIZE);
		outputHID[0 + isBluetooth] = 0x02;
		outputHID[1 + isBluetooth] = 0x03 | 0x04 | 0x08;
		outputHID[2 + isBluetooth] = 0x55;
		add_crc_to_buffer(outputHID);
		hid_write(device, outputHID, isBluetooth ? sizeof(outputHID) : USB_BUFFER_SIZE);
	}

public:
	void sync()
	{
		getDualSenseInput();
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
		setDualSenseState();
	}

	~Bridge()
	{
		cleanControllerState();
		hid_close(device);
		vigem_target_remove(vigemClient, virtualController);
		vigem_target_free(virtualController);
	}
};

class BridgeManager
{
	BridgeManager(const BridgeManager &) = delete;
	BridgeManager &operator=(const BridgeManager &) = delete;
	std::unordered_set<std::unique_ptr<Bridge>> bridges{};
	PVIGEM_CLIENT vigemClient = vigem_alloc();
	hid_hotplug_callback_handle hotplugHandle = 0;
	hid_hotplug_callback_handle hotplugEdgeHandle = 0;

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
		bridges.insert(std::make_unique<Bridge>(vigemClient, deviceInfo));
	}

	void remove(hid_device_info *deviceInfo)
	{
		std::erase_if(bridges, [&deviceInfo](const auto &bridge)
					  { return bridge->matches(deviceInfo); });
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
			for (auto &bridge : bridges)
				bridge->sync();
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
			ShellExecuteW(0, 0, L"joy.cpl", 0, 0, SW_SHOW);
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

	HICON hIcon{};
	ExtractIconExW(L"joy.cpl", 0, &hIcon, nullptr, 1);
	notifyIconData.hIcon = hIcon;
	notifyIconData.cbSize = sizeof(NOTIFYICONDATAW);
	notifyIconData.uCallbackMessage = notifyClickId;
	notifyIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	notifyIconData.hWnd = hWnd;
	wcscpy_s(notifyIconData.szTip, L"DualSense to XInput");
	Shell_NotifyIconW(NIM_ADD, &notifyIconData);

	MSG msg{};
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
