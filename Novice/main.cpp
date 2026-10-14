#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Novice.h>
#include <cstdint>
#include <cstring>
#include <imgui.h>
#include <random>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

// 送受信するメッセージの種類
enum class MassageType : uint16_t { Text = 1 };

// 受信した文字列の末尾にランダムな英数字を追加する
std::string AddRandomString(const std::string& text) {
	static const char chars[] = "0123456789"
	                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                            "abcdefghijklmnopqrstuvwxyz";

	static std::random_device rd;
	static std::mt19937 engine(rd());

	std::uniform_int_distribution<int> lengthDist(5, 10);
	std::uniform_int_distribution<int> charDist(0, static_cast<int>(sizeof(chars) - 2));

	int length = lengthDist(engine);

	std::string result = text;

	for (int i = 0; i < length; ++i) {
		result += chars[charDist(engine)];
	}

	return result;
}

// Windowsアプリのmain関数。ImGuiを使うため、Debugで実行する。
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize("NT1_01_02", 1280, 740);
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
			// 送信する文字列のバイト数を取得する
			int sendSize = static_cast<int>(strlen(text));

			// 空文字列でなければ送信する
			if (sendSize > 0) {
				int sent = send(sock, text, sendSize, 0);

				if (sent != SOCKET_ERROR && sent > 0) {
					// 実際に送信できたバイト数を加算する
					totalBytesSent += sent;

					// サーバーからの返信を受信する
					char buffer[256]{};
					int received = recv(sock, buffer, sizeof(buffer) - 1, 0);

					if (received > 0) {
						// 受信した文字列の末尾に終端文字を追加
						buffer[received] = '\0';

						// 受信した文字列の末尾にランダムな英数字を追加する
						std::string reply = AddRandomString(buffer);

						// ランダム文字列を追加した内容を保存する
						strcpy_s(receivedText, sizeof(receivedText), reply.c_str());

						// 受信したバイト数を加算する
						totalBytesReceived += received;
					}
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