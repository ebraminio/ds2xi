#include "main.h"
extern std::string Version = "PCXSenseBeta.0.9.4";

extern void (*getInputs)(controller& x360Controller) = &getDualsenseInput;
extern std::string currentDirectory{};

static int initializeFakeController(PVIGEM_TARGET& emulateX360, VIGEM_ERROR& target, PVIGEM_CLIENT& client) {

	if (client == nullptr) return -1;

	const auto retval = vigem_connect(client);

	if (!VIGEM_SUCCESS(retval)) return -1;

	emulateX360 = vigem_target_x360_alloc();

	target = vigem_target_add(client, emulateX360);

	return 0;
}

void debugData(controller& x360Controller) {

	while (true) {

		Sleep(20);
		system("cls"); //Clear console

		std::cout << "\nLeftJoystick Horizontal Value: " << (int)((x360Controller.inputBuffer[1 + x360Controller.hidOffset] * 257) - 32768) << '\n';
		std::cout << "LeftJoystick Vertical Value: " << (int)(32767 - (x360Controller.inputBuffer[2 + x360Controller.hidOffset] * 257)) << '\n';
		std::cout << "RightJoystick Horizontal Value: " << (int)((x360Controller.inputBuffer[3 + x360Controller.hidOffset] * 257) - 32768) << '\n';
		std::cout << "RightJoystick Vertical Value: " << (int)(32767 - (x360Controller.inputBuffer[4 + x360Controller.hidOffset] * 257)) << '\n';
		std::cout << "Left Trigger Value: " << (int)x360Controller.ControllerState.Gamepad.bLeftTrigger << '\n';
		std::cout << "Right Trigger Value: " << (int)x360Controller.ControllerState.Gamepad.bRightTrigger << '\n';
		std::cout << "Battery Level: " << x360Controller.batteryLevel << "%\n";
		std::cout << "Buttons Reading: " << x360Controller.ControllerState.Gamepad.wButtons << "\n";

		switch ((int)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & 0x0f)) {

		case 0: printf("Dpad Up\n"); break;

		case 1: printf("Dpad Up and Dpad Right\n"); break;

		case 2: printf("Dpad Right\n"); break;

		case 3: printf("Dpad Down and Dpad Right\n"); break;

		case 4: printf("Dpad Down\n"); break;

		case 5: printf("Dpad Down and Dpad Left\n"); break;

		case 6: printf("Dpad Left\n"); break;

		case 7: printf("Dpad Up and Dpad Left\n"); break;
		}

		if ((bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 4))) printf("Square Button\n");

		if ((bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 5))) printf("Cross Button\n");

		if ((bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 6))) printf("Circle Button\n");

		if ((bool)(x360Controller.inputBuffer[8 + x360Controller.hidOffset] & (1 << 7))) printf("Triangle Button\n");

		if ((bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 0))) printf("L1 Button\n");

		if ((bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 1))) printf("R1 Button\n");

		if ((bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 4))) printf("Select Button\n");

		if ((bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 5))) printf("Start Button\n");

		if ((bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 6))) printf("L3 Button\n");

		if ((bool)(x360Controller.inputBuffer[9 + x360Controller.hidOffset] & (1 << 7))) printf("R3 Button\n");

		if ((bool)(x360Controller.inputBuffer[10 + x360Controller.hidOffset] & (1 << 0))) printf("Sony/Home Button\n");

		if ((bool)(x360Controller.inputBuffer[10 + x360Controller.hidOffset] & (1 << 1))) printf("Touchpad Button\n");

		if ((bool)(x360Controller.inputBuffer[10 + x360Controller.hidOffset] & (1 << 2))) printf("Mic Button\n");

		if (!x360Controller.isConnected)
			printf("Failed to get device state\n");
	}
}

int main(int argc,char* argv[]) {
	currentDirectory = std::filesystem::path(argv[0]).parent_path().string();
#ifdef NDEBUG
	autoUpdater();
#endif
	SetProcessShutdownParameters(2, 0);
	SetConsoleCtrlHandler(exitFunction, TRUE);
	
	//Initialize Fake Controller
	controller x360Controller{};

	std::vector<Macros> Macro;
	std::vector<gameProfile> gameProfiles;

	loadMacros(Macro);
	loadProfiles(gameProfiles);
	triggerToProfile(gameProfiles);
	loadLightSettings(x360Controller);
	
	x360Controller.client = vigem_alloc();

	if (x360Controller.client == NULL) {
		if (MessageBox(NULL, L"The app couldn't start, please install VigemBusDriver ,if this error persists please open an issue on github", L"Vigem bus", MB_YESNO | MB_TASKMODAL) == IDNO) return -1;
		ShellExecute(0, 0, L"https://github.com/nefarius/ViGEmBus/releases/tag/v1.22.0", 0, 0, SW_SHOW);
		return -1;
	}
	
	if (initializeFakeController(x360Controller.emulateX360, x360Controller.target, x360Controller.client) != 0) {
		if (MessageBox(NULL, L"The app couldn't start, please install VigemBusDriver ,if this error persists please open an issue on github", L"Vigem bus", MB_YESNO | MB_TASKMODAL) == IDNO) return -1;
		ShellExecute(0, 0, L"https://github.com/nefarius/ViGEmBus/releases/tag/v1.22.0", 0, 0, SW_SHOW);
		return -1;
	}
	//Start async threads
	std::thread(GUI, std::ref(x360Controller),std::ref(Macro),std::ref(gameProfiles)).detach();
	std::thread(asyncMacro, std::ref(x360Controller),std::ref(Macro)).detach();
	std::thread(asyncGameProfile, std::ref(gameProfiles), std::ref(x360Controller)).detach();
	//std::thread(secondController,std::ref(x360Controller2)).detach();

#if _DEBUG
	std::thread(debugData, std::ref(x360Controller)).detach(); // Displays controller info
#endif

	ptrController = &x360Controller;
//	ptrController2 = &x360Controller2;
	ptrMacros = &Macro;
	ptrProfiles = &gameProfiles;

	vigem_target_x360_register_notification(x360Controller.client, x360Controller.emulateX360, &getRumble, ptrController);
	//vigem_target_x360_register_notification(x360Controller2.client, x360Controller2.emulateX360, &getRumble, ptrController2);
	//hideDevice();
	isControllerConnected(x360Controller);
	while (true) {
		XInputGetState(0, &x360Controller.ControllerState);

		getInputs(x360Controller);

		vigem_target_x360_update(x360Controller.client, x360Controller.emulateX360, *reinterpret_cast<XUSB_REPORT*>(&x360Controller.ControllerState.Gamepad));

	}

	return 0;
}
