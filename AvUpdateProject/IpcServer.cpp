#include "IpcServer.h"
#include <windows.h>
#include <iostream>
#include <thread>
#include <string>
#include "UpdaterCore.h"
#include "UpdateState.h"
#include "AsyncEngine.h"

namespace IpcServer {
    void HandleClientCommand(HANDLE hPipe) {
        try { // BỌC THÉP LUỒNG PIPE
            char buffer[1024];
            DWORD bytesRead;

            if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                std::string cmd(buffer);
                cmd.erase(cmd.find_last_not_of(" \n\r\t") + 1); // Xóa dấu Enter thừa

                if (cmd == "update") {
                    if (!UpdateState::IS_UPDATING.load()) {
                        std::cout << "[IPC] Nhan lenh UPDATE tu Client. Dang kiem tra..." << std::endl;

                        // MA THUẬT Ở ĐÂY: Hứng kết quả (true/false) từ Sếp (UpdaterCore)
                        bool isUpdateStarted = UpdaterCore::ExecuteUpdate();

                        std::string res;
                        if (isUpdateStarted) {
                            res = "OK_STARTING\n"; // Sếp bẩu tải -> Báo Client chuẩn bị Stream
                        }
                        else {
                            // Sếp bẩu hủy (do đã mới nhất hoặc AV đang quét) -> Báo thẳng cho Client
                            res = "NO_UPDATE_NEEDED\n";
                        }

                        DWORD bytesWritten;
                        WriteFile(hPipe, res.c_str(), res.length(), &bytesWritten, NULL);
                    }
                    else {
                        std::string res = "ERR_ALREADY_UPDATING\n";
                        DWORD bytesWritten;
                        WriteFile(hPipe, res.c_str(), res.length(), &bytesWritten, NULL);
                    }
                }
                // ===== THÊM NHÁNH NHẬN LỆNH HẠ CẤP =====
                else if (cmd == "rollback") {
                    if (!UpdateState::IS_UPDATING.load()) {
                        std::cout << "[IPC] Nhan lenh HA CAP tu Client. Dang kich hoat Rollback..." << std::endl;

                        // Bật luồng chạy hàm Hạ cấp
                        std::thread rollbackThread(AsyncEngine::ManualRollbackTask);
                        rollbackThread.detach();

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
                // ========================================
                else if (cmd == "stream") {
                    std::cout << "[IPC] Client dang yeu cau Stream Tien do..." << std::endl;
                    // Vòng lặp bơm máu % liên tục
                    while (UpdateState::IS_UPDATING.load()) {
                        std::string statusJson = UpdateState::GetStatusJson() + "\n";
                        DWORD bytesWritten;
                        if (!WriteFile(hPipe, statusJson.c_str(), statusJson.length(), &bytesWritten, NULL)) {
                            break; // Ống vỡ (Python ngắt kết nối) thì dừng
                        }
                        Sleep(500); // Ngủ nửa giây tránh nghẽn
                    }
                    // Bơm nhát cuối cùng báo cáo 100% hoặc báo lỗi
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

        // =================================================================
        // MA THUẬT MỞ KHÓA QUYỀN (CHO PHÉP PYTHON GIAO TIẾP VỚI C++ ADMIN)
        // =================================================================
        SECURITY_DESCRIPTOR sd;
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE); // NULL DACL: Mở cửa đại hội!

        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = FALSE;
        // =================================================================

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