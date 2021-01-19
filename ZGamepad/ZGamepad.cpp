// Reduce unnecessary winapi's #includes
#define WIN32_LEAN_AND_MEAN
// Exclude winapi's std::min and std::max macros to let uwebsockets compile successfully
#define NOMINMAX
// Include winapi's definitions for ViGEm to function properly
#include <windows.h>
// Include Xinput API to interact with ViGEm's emulated Xbox controllers
#include <Xinput.h>
#include <ViGEm/Client.h>
#include <uwebsockets/App.h>
#include <iostream>
// Statically link SetupAPI against ViGEm as per its readme
#pragma comment(lib, "setupapi.lib")
// Fixes unresolved external symbol XInputGetState
#pragma comment(lib, "XInput.lib")

struct PerSocketData {
	unsigned char controller_id;
};

int main() {
	// Allocates a block of memory for the emulator
	const auto client = vigem_alloc();

	if(client == nullptr) {
		std::cerr << "Not enough memory available to allocate." << std::endl;
		return -1;
	}

	const auto retval = vigem_connect(client);

	if(!VIGEM_SUCCESS(retval)) {
		std::cerr << "ViGEm Bus connection failed with error code: 0x" << std::hex << retval << std::endl;
		return -1;
	}

	// Allocates handle for a new Xinput controller
	// Move this section into ws.open later (per controller)
	const auto pad = vigem_target_x360_alloc();
	const auto pir = vigem_target_add(client, pad);

	if(!VIGEM_SUCCESS(pir)) {
		std::cerr << "Target plugin failed with error code: 0x" << std::hex << pir << std::endl;
		return -1;
	}

	// Declares and initializes a state that'll send inputs to the emulated controller
	// The initial values don't really matter, this just makes sure they're initialized to begin with
	XINPUT_STATE state;
	XInputGetState(0, &state);

	// Launch the WebSocket mini-server which relays a controller's state over to the emulator
	// The GET request for regular browsers brings up a control panel to manage controllers
	// The /controller endpoint is an upgraded GET request to initialize a controller
	// The /panel endpoint is an upgraded GET request to capture information for the control panel
	uWS::App().get("/*", [&client, &pad, &state](auto* res, auto* req) {
		res->end("Hello world!");
	}).ws<PerSocketData>("/controller", {
		.upgrade = [](auto* res, auto* req, auto* context) {
			res->template upgrade<PerSocketData>({
				.controller_id = 3
			}, req->getHeader("sec-websocket-key"),
				req->getHeader("sec-websocket-protocol"),
				req->getHeader("sec-websocket-extensions"),
			context);
		},
		.open = [](auto* ws) {
			std::cout << "Controller ID: " << (int)static_cast<PerSocketData*>(ws->getUserData())->controller_id << std::endl;
		},
		.message = [&client, &pad, &state](auto* ws, std::string_view message, uWS::OpCode opCode) {
			if(opCode == uWS::OpCode::BINARY) {
				// 1 packet = 12 bytes (little-endian)
				// wButtons [00 00] bLeftTrigger [00] bRightTrigger [00] sThumbLX [00 00] sThumbLY [00 00] sThumbRX [00 00] sThumbRY [00 00]
				if(message.length() == 12) {
					state.Gamepad.wButtons = (message[1] << 8) + message[0];
					state.Gamepad.bLeftTrigger = message[2];
					state.Gamepad.bRightTrigger = message[3];
					state.Gamepad.sThumbLX = (message[5] << 8) + message[4];
					state.Gamepad.sThumbLY = (message[7] << 8) + message[6];
					state.Gamepad.sThumbRX = (message[9] << 8) + message[8];
					state.Gamepad.sThumbRY = (message[11] << 8) + message[10];
					vigem_target_x360_update(client, pad, *reinterpret_cast<XUSB_REPORT*>(&state.Gamepad));
				} else {
					ws->send("Your payload must conform to exactly 12 bytes.", uWS::OpCode::TEXT);
				}
			} else {
				ws->send("Please send data in the specified binary format.", opCode);
			}
		}
	}).listen(3000, [](auto* listen_socket) {
		if(listen_socket) {
			std::cout << "Listening on port " << 3000 << std::endl;
		}
	}).run();

	// Disconnects and frees the controller
	// Move this into ws.close later
	vigem_target_remove(client, pad);
	vigem_target_free(pad);

	// Disconnects and frees the client
	vigem_disconnect(client);
	vigem_free(client);

	// According to uWebSocket's user guide, the event loop should never actually stop
	std::cout << "Failed to listen on port 3000" << std::endl;
	return -1;
}
