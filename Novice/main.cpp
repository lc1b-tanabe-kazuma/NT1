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

enum class MessageType : uint16_t { Text = 1, AssignPlayerId = 2, JoinRequest = 3, WorldState = 4, Disconnect = 5, PlayerInput = 6 };

struct PlayerState {
	uint16_t id;
	uint16_t x;
	uint16_t y;
};

// キーボード入力の状態を保持する構造体
char keys[256];

// メッセージを送信待ちキューに追加する
void QueueMessage(std::vector<char>& sendQueue, MessageType type, const void* payload, uint16_t payloadSize) {
	const size_t oldSize = sendQueue.size();
	const size_t frameSize = kHeaderSize + payloadSize;

	// メッセージ分の領域を確保
	sendQueue.resize(oldSize + frameSize);

	char* frame = sendQueue.data() + oldSize;

	// ヘッダーを設定
	std::memcpy(frame, &payloadSize, sizeof(payloadSize));
	std::memcpy(frame + 2, &type, sizeof(type));

	// 本文がある場合はコピー
	if (payloadSize > 0 && payload != nullptr) {
		std::memcpy(frame + kHeaderSize, payload, payloadSize);
	}
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize(kWindowTitle, kScreenWidth, kScreenHeight);

	WSADATA wsaData{};

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		Novice::Finalize();
		return 1;
	}

	SOCKET sock = INVALID_SOCKET;
	bool connected = false;

	char ipAddress[64] = "34.104.202.113";
	int port = 8000;

	uint16_t myPlayerId = 0;

	std::vector<PlayerState> players;

	// 送信待ちデータ
	std::vector<char> sendQueue;

	// 送信済みのバイト数
	size_t sendOffset = 0;

	// 受信バッファはゲームループの外で保持する。
	// TCPでは1回の recv で1メッセージ全部が届くとは限らない。
	char receiveBuffer[kBufferSize]{};
	int receivedBytes = 0;

	// ループ回数を保持する変数
	int loopCount = 0;

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();
		loopCount++;

		// キーボード入力の状態を更新する
		Novice::GetHitKeyStateAll(keys);

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
					// ノンブロッキングに切り替える
					u_long nonBlocking = 1;

					if (ioctlsocket(sock, FIONBIO, &nonBlocking) == 0) {

						connected = true;

						receivedBytes = 0;
						players.clear();
						myPlayerId = 0;

						sendQueue.clear();
						sendOffset = 0;

						// 接続直後にJoinRequestを送信待ちに追加
						QueueMessage(sendQueue, MessageType::JoinRequest, nullptr, 0);

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
				connected = false;

				shutdown(sock, SD_BOTH);
				closesocket(sock);

				sock = INVALID_SOCKET;

				receivedBytes = 0;
				players.clear();
				myPlayerId = 0;

				sendQueue.clear();
				sendOffset = 0;
			}
		}

		ImGui::End();

		// サーバーからの受信
		if (connected) {

			// WASD入力を取得
			uint16_t inputBits = 0;

			if (keys[DIK_W]) {
				inputBits |= 0x0001;
			}

			if (keys[DIK_S]) {
				inputBits |= 0x0002;
			}

			if (keys[DIK_A]) {
				inputBits |= 0x0004;
			}

			if (keys[DIK_D]) {
				inputBits |= 0x0008;
			}

			// PlayerInputを送信待ちに追加
			QueueMessage(sendQueue, MessageType::PlayerInput, &inputBits, sizeof(inputBits));

			while (sendOffset < sendQueue.size()) {

				int remaining = static_cast<int>(sendQueue.size() - sendOffset);

				int sent = send(sock, sendQueue.data() + sendOffset, remaining, 0);

				if (sent > 0) {

					// 送信できた分だけ位置を進める
					sendOffset += sent;

				} else {

					int error = WSAGetLastError();

					if (sent == SOCKET_ERROR && error == WSAEWOULDBLOCK) {

						// 今は送信できないので次のフレームに持ち越す
						break;

					} else {

						// その他の送信エラー
						connected = false;
						break;
					}
				}
			}

			// 送信済みのデータをキューから削除する
			if (connected) {
				if (sendOffset > 0) {

					// 送信済みのデータを削除する
					sendQueue.erase(sendQueue.begin(), sendQueue.begin() + sendOffset);

					sendOffset = 0;
				}
			}

			// 接続が切断された場合、ソケットを閉じて状態をリセットする
			if (!connected) {

				shutdown(sock, SD_BOTH);
				closesocket(sock);

				sock = INVALID_SOCKET;

				sendQueue.clear();
				sendOffset = 0;
				receivedBytes = 0;
				players.clear();
				myPlayerId = 0;
			}

			// 受信処理
			bool shouldDisconnect = false;

			// 受信バッファに空きがある限り、データを受信する
			while (true) {
				const int available = kBufferSize - receivedBytes;

				if (available <= 0) {
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

			// 受信バッファに1フレーム分のデータが揃っている限り、メッセージを処理する
			while (!shouldDisconnect && receivedBytes >= kHeaderSize) {
				uint16_t receivePayloadSize = 0;
				MessageType receiveType{};

				std::memcpy(&receivePayloadSize, receiveBuffer, sizeof(receivePayloadSize));

				std::memcpy(&receiveType, receiveBuffer + 2, sizeof(receiveType));

				if (receivePayloadSize > kMaxPayloadSize) {
					shouldDisconnect = true;
					break;
				}

				const int receiveFrameSize = kHeaderSize + receivePayloadSize;

				if (receivedBytes < receiveFrameSize) {
					break;
				}

				// 受信したメッセージの本文を処理する。
				const char* payload = receiveBuffer + kHeaderSize;

				switch (receiveType) {
					// メッセージ処理
				case MessageType::Text:
					// 必要に応じてテキストを処理する。
					break;

				case MessageType::AssignPlayerId:

					// プレイヤーIDを読み取る
					if (receivePayloadSize == sizeof(myPlayerId)) {
						std::memcpy(&myPlayerId, payload, sizeof(myPlayerId));
					} else {
						shouldDisconnect = true;
					}

					break;

				case MessageType::WorldState: {
					// 人数を読み取る
					if (receivePayloadSize < sizeof(uint16_t)) {
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
					if (count <= 128 && receivePayloadSize == expectedSize) {
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
					receivedBytes -= receiveFrameSize;

					std::memmove(receiveBuffer, receiveBuffer + receiveFrameSize, receivedBytes);
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
			uint32_t color = player.id == myPlayerId ? 0xFF0000FF : 0x55BD79FF;
			Novice::DrawEllipse(static_cast<int>(player.x), static_cast<int>(player.y), 20, 20, 0.0f, color, kFillModeSolid);

			// 自分のプレイヤーの周りにリングを描画する
			if (player.id == myPlayerId) {
				int ring = 20 + static_cast<int>(loopCount % 30);
				Novice::DrawEllipse(static_cast<int>(player.x), static_cast<int>(player.y), ring, ring, 0.0f, 0xFFFFFFFF, kFillModeWireFrame);
			}
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