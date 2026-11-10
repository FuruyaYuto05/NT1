#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Novice.h>
#include <imgui.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>

#pragma comment(lib, "Ws2_32.lib")

// TCPでは1回で全量を送受信できるとは限らないため、残りを繰り返す。
bool SendAll(SOCKET socket, const char* data, int length, int& total, int& error) {
	int offset = 0;
	while (offset < length) {
		const int count = send(socket, data + offset, length - offset, 0);
		if (count == SOCKET_ERROR) {
			error = WSAGetLastError();
			return false;
		}
		if (count == 0) { return false; }
		offset += count;
		total += count;
	}
	return true;
}

bool ReceiveAll(SOCKET socket, char* data, int length, int& total, int& error) {
	int offset = 0;
	while (offset < length) {
		const int count = recv(socket, data + offset, length - offset, 0);
		if (count == SOCKET_ERROR) {
			error = WSAGetLastError();
			return false;
		}
		if (count == 0) { return false; } // 相手が切断した。
		offset += count;
		total += count;
	}
	return true;
}
// Windowsアプリのmain関数。ImGuiを使うため、Debugで実行する。
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize("NT1_01_00", 640, 360);
	char host[64] = "34.104.202.113";
	char port[8] = "8000"; // ヘッダー付き通信のポート
	bool connected = false;
	char text[1024] = "Hello";
	char reply[4096] = {};
	int sentBytes = 0;
	int receivedBytes = 0;
	int communicationError = 0;
	const char* result = "Ready";

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
		ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_Once);
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
						// 応答が来ない場合も無期限に待たず、エラーとして扱う。
						DWORD timeoutMs = 5000;
						setsockopt(tcpSocket, SOL_SOCKET, SO_RCVTIMEO,
							reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
						setsockopt(tcpSocket, SOL_SOCKET, SO_SNDTIMEO,
							reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
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
		ImGui::Separator();
		ImGui::InputText("Text", text, sizeof(text));
		ImGui::BeginDisabled(!connected);
		if (ImGui::Button("Send")) {
			sentBytes = 0;
			receivedBytes = 0;
			communicationError = 0;
			reply[0] = '\0';
			const int length = static_cast<int>(std::strlen(text));

			// 4バイトヘッダー: 本文長の下位・上位バイト、Text種別1、0。
			// このサーバーは下位バイトから送る形式。構造体の配置には依存しない。
			char frame[4 + sizeof(text)] = {};
			frame[0] = static_cast<char>(length & 0xFF);
			frame[1] = static_cast<char>((length >> 8) & 0xFF);
			frame[2] = 1;
			frame[3] = 0;
			std::memcpy(frame + 4, text, length);

			bool success = SendAll(tcpSocket, frame, 4 + length,
				sentBytes, communicationError);
			result = success ? "Receiving header" : "Send failed / disconnected";

			unsigned char header[4] = {};
			if (success) {
				success = ReceiveAll(tcpSocket, reinterpret_cast<char*>(header), 4,
					receivedBytes, communicationError);
				if (!success) { result = "Header receive failed / disconnected"; }
			}
			if (success) {
				const int bodyLength = static_cast<int>(header[0]) |
					(static_cast<int>(header[1]) << 8);
				// 終端文字の1バイトを確保し、異常な長さは受信前に拒否する。
				if (bodyLength >= static_cast<int>(sizeof(reply))) {
					success = false;
					result = "Reply body too large";
				} else {
					success = ReceiveAll(tcpSocket, reply, bodyLength,
						receivedBytes, communicationError);
					if (!success) {
						reply[0] = '\0';
						result = "Body receive failed / disconnected";
					} else if (header[2] != 1 || header[3] != 0) {
						// 本文は消費済みなので、次のフレームも正しく読める。
						reply[0] = '\0';
						result = "Received a non-Text frame";
					} else {
						reply[bodyLength] = '\0';
						result = "Text frame received";
					}
				}
			}
			if (!success) {
				closesocket(tcpSocket);
				tcpSocket = INVALID_SOCKET;
				connected = false;
			}
		}
		ImGui::EndDisabled();
		ImGui::TextWrapped("Reply: %s", reply);
		ImGui::Text("Sent: %d bytes / Received: %d bytes", sentBytes, receivedBytes);
		ImGui::Text("Total: %d bytes", sentBytes + receivedBytes);
		ImGui::Text("Result: %s", result);
		if (communicationError != 0) {
			ImGui::Text("Winsock error: %d", communicationError);
		}
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
