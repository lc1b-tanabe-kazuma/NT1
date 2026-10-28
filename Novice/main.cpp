#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Novice.h>
#include <cstdint>
#include <cstring>
#include <imgui.h>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

// メッセージ種別を定義する
enum class MessageType : uint16_t { Text = 1, AssignPlayerId = 2, JoinRequest = 3, WorldState = 4, Disconnect = 5 };

// Windowsアプリのmain関数。ImGuiを使うため、Debugで実行する。
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize("NT1_01_00", 1280, 740);
	char host[64] = "34.104.202.113";
	char port[8] = "8000";
	bool connected = false;

	// 通信用の変数を用意し、Winsockを初期化する。
	WSADATA wsaData;
	SOCKET sock = INVALID_SOCKET;

	// 送信する文字列
	char text[256] = "";

	// サーバーから受信した文字列
	char receivedText[256] = "";

	// 送信・受信したバイト数
	int totalBytesSent = 0;
	int totalBytesReceived = 0;

	// Winsockを初期化する
	WSAStartup(MAKEWORD(2, 2), &wsaData); // windowsの通信機能を準備する

	// プレイヤー
	struct PlayerState {
		uint16_t id, x, y;
	};
	std::vector<PlayerState> players;

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();

		// ImGuiの処理など
#ifdef USE_IMGUI
		ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
		ImGui::SetNextWindowSize(ImVec2(600, 200), ImGuiCond_Once);
		ImGui::Begin("TCP Connection");
		ImGui::BeginDisabled(connected);
		ImGui::InputText("Host", host, sizeof(host));
		ImGui::InputText("Port", port, sizeof(port));
		if (ImGui::Button("Connect")) {

			// 接続先を取得し、ソケットを作って接続する。
			addrinfo hints{};
			addrinfo* result = nullptr;

			// TCP通信の設定
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			// 接続先の情報を取得する
			if (getaddrinfo(host, port, &hints, &result) == 0) {

				// ソケットを作成する
				sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

				// ソケットの作成に成功した場合
				if (sock != INVALID_SOCKET) {

					// サーバーに接続する
					if (connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) == 0) {
						connected = true;

						u_long nonBlocking = 1;

						if (ioctlsocket(sock, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
							closesocket(sock);
							sock = INVALID_SOCKET;
							connected = false;
						} else {
							connected = true;

							// 参加リクエストを送る
							uint16_t payloadSize = 0;
							MessageType type = MessageType::JoinRequest;
							char sendFrame[4]{};
							std::memcpy(sendFrame, &payloadSize, sizeof(payloadSize));
							std::memcpy(sendFrame + 2, &type, sizeof(type));

							// 4バイト送りきる
							int sentBytes = 0;
							while (connected && sentBytes < 4) {
								int sent = send(sock, sendFrame + sentBytes, 4 - sentBytes, 0);
								if (sent == SOCKET_ERROR || sent == 0) {
									connected = false;
									break;
								}
								sentBytes += sent;
							}
						}
					} else {
						// 接続に失敗した場合はソケットを閉じる
						closesocket(sock);
						sock = INVALID_SOCKET;
					}
				}

				// 接続先の情報を解放する
				freeaddrinfo(result);
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!connected);

		// 接続中の場合は切断ボタンを有効にする
		if (ImGui::Button("Disconnect")) {
			// 通信を終了する
			shutdown(sock, SD_BOTH);

			// ソケットを閉じる
			closesocket(sock);

			// 無効なソケットに戻す
			sock = INVALID_SOCKET;

			// 未接続状態にする
			connected = false;
		}
		ImGui::EndDisabled();
		ImGui::Text("Status: %s", connected ? "Connected" : "Not connected");

		if (connected) {
			// 毎フレーム受信データを確認する
			char receiveFrame[1028]{};
			int receivedBytes = 0;

			// データを受信する
			while (true) {
				int received = recv(sock, receiveFrame + receivedBytes, static_cast<int>(sizeof(receiveFrame)) - receivedBytes, 0);

				if (received > 0) {
					receivedBytes += received;
				} else if (received == 0) {
					// サーバーが接続を切断した
					connected = false;
					break;
				} else {
					// ノンブロッキングソケットの場合、
					// WSAEWOULDBLOCK は「今はデータがない」
					if (WSAGetLastError() == WSAEWOULDBLOCK) {
						break;
					}

					// その他の通信エラー
					connected = false;
					break;
				}

				// バッファがいっぱいになった
				if (receivedBytes == sizeof(receiveFrame)) {
					break;
				}
			}

			// 受信したデータからフレームを取り出す
			while (connected && receivedBytes >= 4) {

				// ヘッダーから本文長と種別を取得する
				uint16_t payloadSize = 0;
				MessageType type{};

				std::memcpy(&payloadSize, receiveFrame, sizeof(payloadSize));
				std::memcpy(&type, receiveFrame + 2, sizeof(type));

				// 本文がバッファの上限を超えている場合は切断する
				if (payloadSize > 1024) {
					connected = false;
					break;
				}

				// ヘッダーと本文が揃っていない場合は次回に持ち越す
				int frameSize = 4 + payloadSize;

				if (receivedBytes < frameSize) {
					break;
				}

				// 本文の先頭アドレス
				const char* payload = receiveFrame + 4;

				// MessageTypeで振り分ける
				switch (type) {

				case MessageType::Text:
					// Textの処理
					break;

				case MessageType::AssignPlayerId:
					// 自分のプレイヤーIDを取得する処理
					break;

				case MessageType::WorldState:
					// プレイヤー一覧を更新する処理
					// payload と payloadSize を使って解析する
					break;

				case MessageType::Disconnect:
					// 切断通知の処理
					break;

				default:
					// 未知のメッセージ種別
					break;
				}

				// 処理済みのフレームを取り除く
				receivedBytes -= frameSize;
				std::memmove(receiveFrame, receiveFrame + frameSize, receivedBytes);
			}

			// 接続が切れた場合はソケットを閉じる
			if (!connected) {
				shutdown(sock, SD_BOTH);
				closesocket(sock);
				sock = INVALID_SOCKET;
				receivedBytes = 0;
			}
		}
		ImGui::End();
#endif
		Novice::EndFrame();
	}

	// 接続が残っていれば閉じ、Winsockを終了する。
	if (sock != INVALID_SOCKET) {
		// 通信を終了する
		shutdown(sock, SD_BOTH);

		// ソケットを閉じる
		closesocket(sock);
	}

	// Winsockを終了する
	WSACleanup();

	Novice::Finalize();
	return 0;
}