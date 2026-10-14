#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Novice.h>
#include <imgui.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

// Windowsアプリのmain関数。ImGuiを使うため、Debugで実行する。
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Novice::Initialize("NT1_01_00", 1280, 740);
	char host[64] = "34.104.202.113";
	char port[8] = "8001"; // 01_00・01_01のEcho用ポート
	bool connected = false;

	// 通信用の変数を用意し、Winsockを初期化する。
	WSADATA wsaData;
	SOCKET sock = INVALID_SOCKET;

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