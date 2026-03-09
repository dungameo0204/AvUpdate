#include "IpcServer.h"
#include <windows.h>
#include <iostream>
#include "UpdaterCore.h"
#include "UpdateState.h" // Nhúng bộ quản lý trạng thái vào

namespace IpcServer {
    void HandleClientCommand(HANDLE hPipe) {
        try { // BỌC THÉP LUỒNG PIPE
            char buffer[1024];
            DWORD bytesRead;

            if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                std::string cmd(buffer);
                cmd.erase(cmd.find_last_not_of(" \n\r\t") + 1);

                if (cmd == "update") {
                    if (!UpdateState::IS_UPDATING.load()) {
                        UpdaterCore::ExecuteUpdate();
                        std::string res = "OK_STARTING\n";
                        DWORD bytesWritten;
                        WriteFile(hPipe, res.c_str(), res.length(), &bytesWritten, NULL);
                    }
                    else {
                        std::string res = "ERR_ALREADY_UPDATING\n";
                        DWORD bytesWritten;
                        WriteFile(hPipe, res.c_str(), res.length(), &bytesWritten, NULL);
                    }
                }
                else if (cmd == "stream") {
                    while (UpdateState::IS_UPDATING.load()) {
                        std::string statusJson = UpdateState::GetStatusJson() + "\n";
                        DWORD bytesWritten;
                        if (!WriteFile(hPipe, statusJson.c_str(), statusJson.length(), &bytesWritten, NULL)) {
                            break;
                        }
                        Sleep(500);
                    }
                    std::string finalJson = UpdateState::GetStatusJson() + "\n";
                    DWORD bytesWritten;
                    WriteFile(hPipe, finalJson.c_str(), finalJson.length(), &bytesWritten, NULL);
                }
            }
        }
        catch (...) {
            std::cerr << "[-] Loi khong xac dinh trong Pipe Client!" << std::endl;
        }

        // Luôn luôn đóng ống cẩn thận dù có lỗi hay không
        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    void StartListening() {
        std::cout << "[*] Dang khoi tao Named Pipe Server tai: \\\\.\\pipe\\NcsAvUpdaterPipe" << std::endl;

        SECURITY_DESCRIPTOR sd;
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);

        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = FALSE;

        while (true) {
            HANDLE hPipe = CreateNamedPipeA(
                "\\\\.\\pipe\\NcsAvUpdaterPipe",
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES, 1024, 1024, 0, &sa);

            if (hPipe != INVALID_HANDLE_VALUE) {
                // ===== [SỬA LỖI RACE CONDITION KINH ĐIỂN CỦA WINDOWS] =====
                BOOL connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

                if (connected) {
                    std::thread clientThread(HandleClientCommand, hPipe);
                    clientThread.detach();
                }
                else {
                    CloseHandle(hPipe);
                }
                // ==========================================================
            }
        }
    }

}