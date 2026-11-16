#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Novice.h>
#include <imgui.h>
#include <cstdint>
#include <vector>
#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

enum class MessageType : uint16_t {
	Text = 1, AssignPlayerId = 2, JoinRequest = 3,
	WorldState = 4, Disconnect = 5,
};
struct PlayerState { uint16_t id, x, y; };

// サーバーの整数は下位バイトから並ぶ。未整列ポインターは使用しない。
uint16_t Read16(const unsigned char* p) {
	return static_cast<uint16_t>(p[0] | (static_cast<unsigned int>(p[1]) << 8));
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize("NT1_01_03", 1280, 720);
	WSADATA wsaData{};
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		Novice::Finalize();
		return 1;
	}
	char host[64] = "34.104.202.113";
	char port[8] = "8000";
	SOCKET tcpSocket = INVALID_SOCKET;
	bool connected = false;
	bool connecting = false;
	int error = 0;
	const char* status = "Not connected";
	std::vector<PlayerState> players; // フレームをまたいで保持する。
	std::vector<unsigned char> receiveBuffer;
	const char joinRequest[4] = {0, 0, 3, 0}; // 本文長0、JoinRequest。
	int joinSent = 0;
	unsigned int gameLoop = 0;
	using Clock = std::chrono::steady_clock;
	auto connectedAt = Clock::now();

	auto disconnect = [&]() {
		if (tcpSocket != INVALID_SOCKET) { closesocket(tcpSocket); }
		tcpSocket = INVALID_SOCKET;
		connected = false;
		connecting = false;
		players.clear();
		receiveBuffer.clear();
		joinSent = 0;
		gameLoop = 0;
	};

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();
#ifdef USE_IMGUI
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(1280, 88), ImGuiCond_Always);
		ImGui::Begin("Connection", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
		ImGui::BeginDisabled(connected || connecting);
		ImGui::SetNextItemWidth(160);
		ImGui::InputText("Host", host, sizeof(host));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(70);
		ImGui::InputText("Port", port, sizeof(port));
		ImGui::SameLine();
		if (ImGui::Button("Connect")) {
			disconnect();
			error = 0;
			addrinfo hints{};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			addrinfo* addresses = nullptr;
			error = getaddrinfo(host, port, &hints, &addresses);
			status = "Address lookup failed";
			if (error == 0) {
				tcpSocket = socket(addresses->ai_family, addresses->ai_socktype, addresses->ai_protocol);
				status = "Socket creation failed";
				if (tcpSocket == INVALID_SOCKET) {
					error = WSAGetLastError();
				} else {
					u_long nonBlocking = 1;
					if (ioctlsocket(tcpSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
						error = WSAGetLastError();
						status = "Nonblocking setup failed";
						disconnect();
					} else {
						const int code = connect(tcpSocket, addresses->ai_addr, static_cast<int>(addresses->ai_addrlen));
						error = code == 0 ? 0 : WSAGetLastError();
						if (code == 0 || error == WSAEWOULDBLOCK) {
							connecting = true;
							connectedAt = Clock::now();
							error = 0;
							status = "Connecting";
						} else {
							status = "Connect failed";
							disconnect();
						}
					}
				}
				freeaddrinfo(addresses);
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!connected && !connecting);
		if (ImGui::Button("Disconnect")) {
			disconnect();
			status = "Not connected";
			error = 0;
		}
		ImGui::EndDisabled();
#endif

		// 接続完了も待ち続けず、毎フレーム確認する。
		if (connecting) {
			fd_set writable, failed;
			FD_ZERO(&writable); FD_ZERO(&failed);
			FD_SET(tcpSocket, &writable); FD_SET(tcpSocket, &failed);
			timeval timeout{};
			const int ready = select(0, nullptr, &writable, &failed, &timeout);
			if (ready == SOCKET_ERROR) {
				error = WSAGetLastError(); status = "Connect check failed"; disconnect();
			} else if (ready > 0) {
				int socketError = 0;
				int size = sizeof(socketError);
				if (getsockopt(tcpSocket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &size) == SOCKET_ERROR) {
					socketError = WSAGetLastError();
				}
				if (socketError != 0) {
					error = socketError; status = "Connect failed"; disconnect();
				} else {
					connecting = false;
					connected = true;
					connectedAt = Clock::now();
					status = "Connected";
				}
			} else if (Clock::now() - connectedAt > std::chrono::seconds(10)) {
				error = WSAETIMEDOUT; status = "Connect timed out"; disconnect();
			}
		}

		// 接続ごとに参加要求を一度だけ送る。部分送信は次のフレームで続ける。
		if (connected && joinSent < 4) {
			const int count = send(tcpSocket, joinRequest + joinSent, 4 - joinSent, 0);
			if (count > 0) { joinSent += count; }
			else if (count == 0) { status = "Server disconnected"; disconnect(); }
			else {
				const int code = WSAGetLastError();
				if (code != WSAEWOULDBLOCK) { error = code; status = "Join send failed"; disconnect(); }
			}
		}

		// 受信できた分だけ蓄積。途中のヘッダー・本文は次の描画フレームへ持ち越す。
		for (int read = 0; connected && joinSent == 4 && read < 16; ++read) {
			unsigned char buffer[1024];
			const int count = recv(tcpSocket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
			if (count == 0) { status = "Server disconnected"; disconnect(); break; }
			if (count == SOCKET_ERROR) {
				const int code = WSAGetLastError();
				if (code != WSAEWOULDBLOCK) { error = code; status = "Receive failed"; disconnect(); }
				break;
			}
			receiveBuffer.insert(receiveBuffer.end(), buffer, buffer + count);
			while (receiveBuffer.size() >= 4) {
				const uint16_t payloadSize = Read16(receiveBuffer.data());
				const auto type = static_cast<MessageType>(Read16(receiveBuffer.data() + 2));
				if (payloadSize > 1024) {
					status = "Invalid payload size"; disconnect(); break;
				}
				const size_t frameSize = 4 + static_cast<size_t>(payloadSize);
				if (receiveBuffer.size() < frameSize) { break; }
				if (type == MessageType::WorldState) {
					const unsigned char* payload = receiveBuffer.data() + 4;
					if (payloadSize < 2) { status = "Invalid WorldState"; disconnect(); break; }
					const uint16_t playerCount = Read16(payload);
					if (playerCount > 128 || payloadSize != 2 + playerCount * 6) {
						status = "Invalid player count / size"; disconnect(); break;
					}
					std::vector<PlayerState> updated;
					updated.reserve(playerCount);
					for (uint16_t i = 0; i < playerCount; ++i) {
						const unsigned char* record = payload + 2 + i * 6;
						updated.push_back({Read16(record), Read16(record + 2), Read16(record + 4)});
					}
					players = std::move(updated);
				}
				// WorldState以外も本文まで消費し、一覧の更新は行わない。
				receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + frameSize);
			}
		}
		if (connected) {
			gameLoop = static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - connectedAt).count());
		}
#ifdef USE_IMGUI
		ImGui::Text("Status: %s | Players: %d | Game loop: %u | Error: %d",
			status, static_cast<int>(players.size()), gameLoop, error);
		ImGui::End();
#endif
		Novice::DrawBox(0, 0, 1280, 720, 0.0f, 0x5B8DBDFF, kFillModeSolid);
		for (const PlayerState& player : players) {
			Novice::DrawEllipse(player.x, player.y, 16, 16, 0.0f, 0x55BD79FF, kFillModeSolid);
		}
		Novice::EndFrame();
	}
	disconnect();
	WSACleanup();
	Novice::Finalize();
	return 0;
}
