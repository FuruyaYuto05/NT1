#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Novice.h>
#include <imgui.h>
#include <cstdint>
#include <vector>
#include <utility>

#pragma comment(lib, "Ws2_32.lib")

enum class MessageType : uint16_t {
	Text = 1, AssignPlayerId = 2, JoinRequest = 3,
	WorldState = 4, Disconnect = 5, PlayerInput = 6,
};
struct PlayerState { uint16_t id, x, y; };

// 整数は下位バイトから送受信する。
uint16_t Read16(const unsigned char* data) {
	return static_cast<uint16_t>(data[0] | (static_cast<unsigned int>(data[1]) << 8));
}

bool SendAll(SOCKET socket, const unsigned char* data, int size, int& error) {
	int offset = 0;
	while (offset < size) {
		const int count = send(socket, reinterpret_cast<const char*>(data + offset), size - offset, 0);
		if (count == SOCKET_ERROR) { error = WSAGetLastError(); return false; }
		if (count == 0) { return false; }
		offset += count;
	}
	return true;
}

bool ReceiveAll(SOCKET socket, unsigned char* data, int size, int& error) {
	int offset = 0;
	while (offset < size) {
		const int count = recv(socket, reinterpret_cast<char*>(data + offset), size - offset, 0);
		if (count == SOCKET_ERROR) { error = WSAGetLastError(); return false; }
		if (count == 0) { return false; }
		offset += count;
	}
	return true;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize("NT1_01_04", 1280, 720);
	WSADATA wsaData{};
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		Novice::Finalize();
		return 1;
	}
	char host[64] = "34.104.202.113";
	char port[8] = "8000";
	SOCKET tcpSocket = INVALID_SOCKET;
	bool connected = false;
	int error = 0;
	const char* status = "Not connected";
	std::vector<PlayerState> players;
	uint16_t playerId = 0;
	char keys[256]{};
	unsigned int gameLoop = 0;

	auto disconnect = [&]() {
		if (tcpSocket != INVALID_SOCKET) { closesocket(tcpSocket); }
		tcpSocket = INVALID_SOCKET;
		connected = false;
		players.clear();
		playerId = 0;
		gameLoop = 0;
	};

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();
		Novice::GetHitKeyStateAll(keys);
#ifdef USE_IMGUI
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(1280, 104), ImGuiCond_Always);
		ImGui::Begin("TCP Client", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
		ImGui::BeginDisabled(connected);
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
				for (addrinfo* address = addresses; address; address = address->ai_next) {
					tcpSocket = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
					if (tcpSocket == INVALID_SOCKET) { error = WSAGetLastError(); continue; }
					if (connect(tcpSocket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
						connected = true;
						error = 0;
						break;
					}
					error = WSAGetLastError();
					closesocket(tcpSocket);
					tcpSocket = INVALID_SOCKET;
				}
				freeaddrinfo(addresses);
				status = connected ? "Connected" : "Connect failed";
				if (connected) {
					// 入力より先に、本文0バイトの参加要求を接続ごとに一度だけ送る。
					const unsigned char joinRequest[4] = {0, 0, 3, 0};
					if (!SendAll(tcpSocket, joinRequest, 4, error)) {
						status = "Join send failed / disconnected";
						disconnect();
					}
				}
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!connected);
		if (ImGui::Button("Disconnect")) {
			disconnect();
			status = "Not connected";
			error = 0;
		}
		ImGui::EndDisabled();
#endif

		if (connected) {
			// 毎ループ0から作るので、キーを離すと無入力も送信される。
			uint16_t inputBits = 0;
#ifdef USE_IMGUI
			const bool acceptInput = !ImGui::GetIO().WantCaptureKeyboard;
#else
			const bool acceptInput = true;
#endif
			if (acceptInput) {
				if (keys[DIK_W]) { inputBits |= 1 << 0; }
				if (keys[DIK_S]) { inputBits |= 1 << 1; }
				if (keys[DIK_A]) { inputBits |= 1 << 2; }
				if (keys[DIK_D]) { inputBits |= 1 << 3; }
			}
			const unsigned char inputFrame[6] = {
				2, 0, 6, 0,
				static_cast<unsigned char>(inputBits & 0xFF),
				static_cast<unsigned char>((inputBits >> 8) & 0xFF)
			};
			if (!SendAll(tcpSocket, inputFrame, 6, error)) {
				status = "Input send failed / disconnected";
				disconnect();
			}
		}

		// ブロッキングで1フレーム分を読み切る。部分受信にも対応する。
		if (connected) {
			unsigned char header[4]{};
			if (!ReceiveAll(tcpSocket, header, 4, error)) {
				status = "Header receive failed / disconnected";
				disconnect();
			} else {
				const uint16_t payloadSize = Read16(header);
				const auto type = static_cast<MessageType>(Read16(header + 2));
				if (payloadSize > 1024) {
					status = "Invalid payload size";
					disconnect();
				} else {
					unsigned char payload[1024]{};
					if (!ReceiveAll(tcpSocket, payload, payloadSize, error)) {
						status = "Body receive failed / disconnected";
						disconnect();
					} else if (type == MessageType::Disconnect) {
						status = "Server disconnected";
						disconnect();
					} else if (type == MessageType::AssignPlayerId) {
						if (payloadSize != 2) {
							status = "Invalid player ID"; disconnect();
						} else {
							playerId = Read16(payload); // 再接続時も通知を保存し直す。
						}
					} else if (type == MessageType::WorldState) {
						const uint16_t count = payloadSize >= 2 ? Read16(payload) : 0;
						if (payloadSize < 2 || count > 128 || payloadSize != 2 + count * 6) {
							status = "Invalid WorldState"; disconnect();
						} else {
							std::vector<PlayerState> updated;
							updated.reserve(count);
							for (uint16_t i = 0; i < count; ++i) {
								const unsigned char* record = payload + 2 + i * 6;
								updated.push_back({Read16(record), Read16(record + 2), Read16(record + 4)});
							}
							players = std::move(updated);
						}
					}
				}
			}
			if (connected) { ++gameLoop; }
		}
#ifdef USE_IMGUI
		ImGui::Text("Status: %s | WASD: move | Game loop: %u | Player ID: %u | Players: %d | Error: %d",
			status, gameLoop, static_cast<unsigned int>(playerId), static_cast<int>(players.size()), error);
		ImGui::End();
#endif
		Novice::DrawBox(0, 0, 1280, 720, 0.0f, 0x5B8DBDFF, kFillModeSolid);
		for (const PlayerState& player : players) {
			const bool isMe = playerId != 0 && player.id == playerId;
			if (isMe) {
				const int radius = 20 + static_cast<int>(gameLoop % 120) / 2;
				Novice::DrawEllipse(player.x, player.y, radius, radius, 0.0f, 0xFFAB40FF, kFillModeWireFrame);
			}
			Novice::DrawEllipse(player.x, player.y, 16, 16, 0.0f,
				isMe ? 0xFFAB40FF : 0x55BD79FF, kFillModeSolid);
		}
		Novice::EndFrame();
	}
	disconnect();
	WSACleanup();
	Novice::Finalize();
	return 0;
}