#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Novice.h>
#include <imgui.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

const char kWindowTitle[] = "TCP Multiplayer";

constexpr int kScreenWidth = 1280;
constexpr int kScreenHeight = 720;
constexpr int kHeaderSize = 4;
constexpr int kMaxPayloadSize = 1024;
constexpr int kBufferSize = kHeaderSize + kMaxPayloadSize;

enum class MessageType : uint16_t { Text = 1, AssignPlayerId = 2, JoinRequest = 3, WorldState = 4, Disconnect = 5 };

struct PlayerState {
	uint16_t id;
	uint16_t x;
	uint16_t y;
};

// メッセージのヘッダーを作成して送信する
bool SendMessage(SOCKET sock, MessageType type) {
	uint16_t payloadSize = 0;

	char frame[kHeaderSize]{};

	std::memcpy(frame, &payloadSize, sizeof(payloadSize));
	std::memcpy(frame + 2, &type, sizeof(type));

	int sentBytes = 0;

	while (sentBytes < kHeaderSize) {
		int sent = send(sock, frame + sentBytes, kHeaderSize - sentBytes, 0);

		if (sent <= 0) {
			return false;
		}

		sentBytes += sent;
	}

	return true;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize(kWindowTitle, kScreenWidth, kScreenHeight);

	if (WSAStartup(MAKEWORD(2, 2), new WSADATA{}) != 0) {
		Novice::Finalize();
		return 1;
	}

	SOCKET sock = INVALID_SOCKET;
	bool connected = false;

	char ipAddress[64] = "34.104.202.113";
	int port = 8000;

	uint16_t myPlayerId = 0;

	std::vector<PlayerState> players;

	// 受信バッファはゲームループの外で保持する。
	// TCPでは1回の recv で1メッセージ全部が届くとは限らない。
	char receiveBuffer[kBufferSize]{};
	int receivedBytes = 0;

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();

		// 接続画面
		ImGui::Begin("TCP Connection");

		ImGui::InputText("Server IP", ipAddress, sizeof(ipAddress));

		ImGui::InputInt("Port", &port);

		if (!connected) {
			if (ImGui::Button("Connect")) {
				addrinfo hints{};
				hints.ai_family = AF_UNSPEC;
				hints.ai_socktype = SOCK_STREAM;
				hints.ai_protocol = IPPROTO_TCP;

				addrinfo* result = nullptr;
				char portText[16]{};
				sprintf_s(portText, "%d", port);

				if (getaddrinfo(ipAddress, portText, &hints, &result) == 0) {
					for (addrinfo* address = result; address != nullptr; address = address->ai_next) {
						SOCKET candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);

						if (candidate == INVALID_SOCKET) {
							continue;
						}

						if (connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
							sock = candidate;
							break;
						}

						closesocket(candidate);
					}

					freeaddrinfo(result);
				}

				if (sock != INVALID_SOCKET) {
					// まず JoinRequest を送る。
					// この時点ではソケットはブロッキング状態。
					if (SendMessage(sock, MessageType::JoinRequest)) {
						u_long nonBlocking = 1;

						if (ioctlsocket(sock, FIONBIO, &nonBlocking) == 0) {
							connected = true;
							receivedBytes = 0;
							players.clear();
							myPlayerId = 0;
						} else {
							closesocket(sock);
							sock = INVALID_SOCKET;
						}
					} else {
						closesocket(sock);
						sock = INVALID_SOCKET;
					}
				}
			}
		} else {
			ImGui::Text("Connected");
			ImGui::Text("My Player ID: %u", myPlayerId);

			if (ImGui::Button("Disconnect")) {
				SendMessage(sock, MessageType::Disconnect);

				shutdown(sock, SD_BOTH);
				closesocket(sock);

				sock = INVALID_SOCKET;
				connected = false;
				receivedBytes = 0;
				players.clear();
				myPlayerId = 0;
			}
		}

		ImGui::End();

		// サーバーからの受信
		if (connected) {
			bool shouldDisconnect = false;

			while (true) {
				const int available = kBufferSize - receivedBytes;

				if (available <= 0) {
					// バッファが満杯なのに処理できない場合は異常。
					shouldDisconnect = true;
					break;
				}

				int received = recv(sock, receiveBuffer + receivedBytes, available, 0);

				if (received > 0) {
					receivedBytes += received;
				} else if (received == 0) {
					// サーバーが接続を切断した。
					shouldDisconnect = true;
					break;
				} else {
					int error = WSAGetLastError();

					if (error == WSAEWOULDBLOCK) {
						// 現時点で受信データがない。
						break;
					}

					shouldDisconnect = true;
					break;
				}
			}

			// バッファ内に完全なメッセージがある間、順番に処理する。
			while (!shouldDisconnect && receivedBytes >= kHeaderSize) {
				uint16_t payloadSize = 0;
				MessageType type{};

				std::memcpy(&payloadSize, receiveBuffer, sizeof(payloadSize));

				std::memcpy(&type, receiveBuffer + 2, sizeof(type));

				if (payloadSize > kMaxPayloadSize) {
					shouldDisconnect = true;
					break;
				}

				const int frameSize = kHeaderSize + payloadSize;

				// メッセージ全体が届くまで処理を待つ。
				if (receivedBytes < frameSize) {
					break;
				}

				const char* payload = receiveBuffer + kHeaderSize;

				switch (type) {
				case MessageType::Text:
					// 必要に応じてテキストを処理する。
					break;

				case MessageType::AssignPlayerId:
					if (payloadSize == sizeof(uint16_t)) {
						std::memcpy(&myPlayerId, payload, sizeof(myPlayerId));
					}
					break;

				case MessageType::WorldState: {
					// 人数を読み取る
					if (payloadSize < sizeof(uint16_t)) {
						shouldDisconnect = true;
						break;
					}

					uint16_t count = 0;
					std::memcpy(&count, payload, sizeof(count));

					// 1人分のデータサイズ
					constexpr size_t kPlayerDataSize = sizeof(PlayerState);

					// 期待する本文のサイズ
					const size_t expectedSize = sizeof(uint16_t) + static_cast<size_t>(count) * kPlayerDataSize;

					// 人数と本文長が合う場合だけ一覧を読む
					if (count <= 128 && payloadSize == expectedSize) {
						players.resize(count);

						for (uint16_t i = 0; i < count; ++i) {
							const char* playerData = payload + sizeof(uint16_t) + static_cast<size_t>(i) * kPlayerDataSize;

							std::memcpy(&players[i].id, playerData, sizeof(players[i].id));

							std::memcpy(&players[i].x, playerData + sizeof(players[i].id), sizeof(players[i].x));

							std::memcpy(&players[i].y, playerData + sizeof(players[i].id) + sizeof(players[i].x), sizeof(players[i].y));
						}
					} else {
						shouldDisconnect = true;
					}

					break;
				}
				case MessageType::Disconnect:
					shouldDisconnect = true;
					break;

				default:
					// 未定義のメッセージ種別。
					shouldDisconnect = true;
					break;
				}

				// 処理済みメッセージを取り除き、
				// 残ったデータをバッファの先頭へ移動する。
				if (!shouldDisconnect) {
					receivedBytes -= frameSize;

					std::memmove(receiveBuffer, receiveBuffer + frameSize, receivedBytes);
				}
			}

			if (shouldDisconnect) {
				shutdown(sock, SD_BOTH);
				closesocket(sock);

				sock = INVALID_SOCKET;
				connected = false;
				receivedBytes = 0;
				players.clear();
				myPlayerId = 0;
			}
		}

		// プレイヤー一覧を描画する
		for (const PlayerState& player : players) {
			uint32_t color = 0x55BD79FF;
			Novice::DrawEllipse(static_cast<int>(player.x), static_cast<int>(player.y), 20, 20, 0.0f, color, kFillModeSolid);
		}

		Novice::EndFrame();
	}

	if (sock != INVALID_SOCKET) {
		shutdown(sock, SD_BOTH);
		closesocket(sock);
	}

	WSACleanup();
	Novice::Finalize();

	return 0;
}