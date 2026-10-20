#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Novice.h>
#include <imgui.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

// Windowsアプリのmain関数。ImGuiを使うため、Debugで実行する。
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize("NT1_01_00", 640, 360);
	char host[64] = "34.104.202.113";
	char port[8] = "8001"; // 01_00・01_01のEcho用ポート
	bool connected = false;

	// 通信用ソケットを未接続の状態で用意する。
	SOCKET tcpSocket = INVALID_SOCKET;
	WSADATA wsaData{};
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		Novice::Finalize();
		return 1;
	}

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();
#ifdef USE_IMGUI
		ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
		ImGui::SetNextWindowSize(ImVec2(600, 200), ImGuiCond_Once);
		ImGui::Begin("TCP Connection");
		ImGui::BeginDisabled(connected);
		ImGui::InputText("Host", host, sizeof(host));
		ImGui::InputText("Port", port, sizeof(port));
		if (ImGui::Button("Connect")) {
			// ホスト名とポート番号からTCPの接続先を取得する。
			addrinfo hints{};
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			addrinfo* addresses = nullptr;
			if (getaddrinfo(host, port, &hints, &addresses) == 0) {
				// 候補を順番に試し、接続できたソケットを保持する。
				for (addrinfo* address = addresses; address != nullptr;
					address = address->ai_next) {
					tcpSocket = socket(address->ai_family, address->ai_socktype,
						address->ai_protocol);
					if (tcpSocket == INVALID_SOCKET) {
						continue;
					}
					if (connect(tcpSocket, address->ai_addr,
						static_cast<int>(address->ai_addrlen)) == 0) {
						connected = true;
						break;
					}
					closesocket(tcpSocket);
					tcpSocket = INVALID_SOCKET;
				}
				freeaddrinfo(addresses);
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!connected);
		if (ImGui::Button("Disconnect")) {
			closesocket(tcpSocket);
			tcpSocket = INVALID_SOCKET;
			connected = false;
		}
		ImGui::EndDisabled();
		ImGui::Text("Status: %s", connected ? "Connected" : "Not connected");
		ImGui::End();
#endif
		Novice::EndFrame();
	}

	if (tcpSocket != INVALID_SOCKET) {
		closesocket(tcpSocket);
	}
	WSACleanup();

	Novice::Finalize();
	return 0;
}
