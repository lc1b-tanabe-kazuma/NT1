#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Novice.h>
#include <cstdint>
#include <cstring>
#include <imgui.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

//
enum class MessageType : uint16_t { Text = 1 };

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

		// 送信する文字列を入力
		ImGui::InputText("Text", text, sizeof(text));

		// 接続中だけSendボタンを有効にする
		ImGui::BeginDisabled(!connected);

		if (ImGui::Button("Send")) {

			// 今回の送受信カウンタをリセット
			totalBytesSent = 0;
			totalBytesReceived = 0;

			// 本文の長さ
			int bodySize = static_cast<int>(strlen(text));

			if (bodySize > 0) {

				// ヘッダー4バイト + 本文
				char sendBuffer[260]{};

				// 先頭2バイト：本文の長さ
				uint16_t length = static_cast<uint16_t>(bodySize);
				memcpy(sendBuffer, &length, sizeof(length));

				// 後ろ2バイト：メッセージ種別
				uint16_t type = static_cast<uint16_t>(MessageType::Text);
				memcpy(sendBuffer + 2, &type, sizeof(type));

				// 4バイトのヘッダーの後ろに本文をコピー
				memcpy(sendBuffer + 4, text, bodySize);

				// 送信するデータ全体のサイズ
				int sendSize = 4 + bodySize;

				// 全データを送信する
				int totalSent = 0;
				bool communicationOK = true;

				while (totalSent < sendSize) {
					int sent = send(sock, sendBuffer + totalSent, sendSize - totalSent, 0);

					if (sent == SOCKET_ERROR || sent == 0) {
						communicationOK = false;
						break;
					}

					totalSent += sent;
					totalBytesSent += sent;
				}

				// -------------------------
				// ヘッダーの受信
				// -------------------------
				if (communicationOK) {
					char header[4]{};
					int totalHeaderReceived = 0;

					// ヘッダー4バイトをすべて受信する
					while (totalHeaderReceived < 4) {
						int received = recv(sock, header + totalHeaderReceived, 4 - totalHeaderReceived, 0);

						if (received <= 0) {
							communicationOK = false;
							break;
						}

						totalHeaderReceived += received;
						totalBytesReceived += received;
					}

					if (communicationOK) {
						// ヘッダーから本文の長さを取得
						uint16_t receivedBodySize = 0;
						memcpy(&receivedBodySize, header, sizeof(receivedBodySize));

						// ヘッダーからメッセージ種別を取得
						uint16_t receivedType = 0;
						memcpy(&receivedType, header + 2, sizeof(receivedType));

						// 本文のサイズがバッファに収まるか確認
						if (receivedBodySize < sizeof(receivedText)) {
							int totalBodyReceived = 0;

							// 本文をすべて受信する
							while (totalBodyReceived < receivedBodySize) {
								int received = recv(sock, receivedText + totalBodyReceived, receivedBodySize - totalBodyReceived, 0);

								if (received <= 0) {
									communicationOK = false;
									break;
								}

								totalBodyReceived += received;
								totalBytesReceived += received;
							}

							if (communicationOK) {
								receivedText[totalBodyReceived] = '\0';
							}
						} else {
							communicationOK = false;
						}
					}
				}

				// -------------------------
				// 通信エラーへの対応
				// -------------------------
				if (!communicationOK) {
					shutdown(sock, SD_BOTH);
					closesocket(sock);
					sock = INVALID_SOCKET;
					connected = false;

					strcpy_s(receivedText, sizeof(receivedText), "Connection closed or communication error");
				}
			}
		}
		ImGui::EndDisabled();

		// 受信した文字列を表示
		ImGui::Text("Received: %s", receivedText);

		// 送受信したバイト数を表示
		ImGui::Text("Sent: %d bytes", totalBytesSent);
		ImGui::Text("Received: %d bytes", totalBytesReceived);
		ImGui::Text("Total: %d bytes", totalBytesSent + totalBytesReceived);
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