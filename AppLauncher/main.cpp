#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

#define SERVICE_NAME "AvLauncherService"

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;
bool g_IsShuttingDown = false;
const int EXIT_CODE_SWITCH_SLOT = 99;

// =====================================================================
// CÁC HÀM TIỆN ÍCH
// =====================================================================
std::string GetAppDir() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return fs::path(buffer).parent_path().string();
}

std::string ReadSlot() {
    std::string configPath = GetAppDir() + "\\slot_config.txt";
    std::ifstream file(configPath);
    std::string slot = "A";
    if (file.is_open()) {
        std::getline(file, slot);
        file.close();
    }
    else {
        std::ofstream out(configPath);
        out << "A";
        out.close();
    }
    if (slot != "A" && slot != "B") slot = "A";
    return slot;
}

void WriteSlot(const std::string& slot) {
    std::string configPath = GetAppDir() + "\\slot_config.txt";
    std::ofstream file(configPath);
    if (file.is_open()) {
        file << slot;
        file.close();
    }
}

// =====================================================================
// BỘ NÃO CHÍNH CỦA SẾP
// =====================================================================
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam) {
    std::string baseDir = GetAppDir();
    bool isRecoveryMode = false;

    std::cout << "[LAUNCHER] Ke Gac Cong da thuc giac va san sang lam viec!" << std::endl;

    while (WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0) {
        std::string currentSlot = ReadSlot();
        std::string exePath = baseDir + "\\Slot_" + currentSlot + "\\AvUpdateProject.exe";

        if (!fs::exists(exePath)) {
            std::cerr << "[!] LOI: Khong tim thay " << exePath << ". 5s sau thu lai..." << std::endl;
            Sleep(5000);
            continue;
        }

        std::cout << "\n[LAUNCHER] Dang chuan bi goi De tai Slot: " << currentSlot << std::endl;

        std::string cmdLine = "\"" + exePath + "\"";
        if (isRecoveryMode) {
            std::cout << "[LAUNCHER] Phat lenh bai: --recovery-mode" << std::endl;
            cmdLine += " --recovery-mode";
            isRecoveryMode = false;
        }

        std::vector<char> cmdBuffer(cmdLine.begin(), cmdLine.end());
        cmdBuffer.push_back('\0');

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        auto startTime = std::chrono::steady_clock::now();

        if (!CreateProcessA(
            NULL,
            cmdBuffer.data(),
            NULL, NULL, FALSE, 0, NULL,
            (baseDir + "\\Slot_" + currentSlot).c_str(),
            &si, &pi
        )) {
            std::cerr << "[-] Goi De that bai! Loi: " << GetLastError() << std::endl;
            std::string fallbackSlot = (currentSlot == "A") ? "B" : "A";
            WriteSlot(fallbackSlot);
            isRecoveryMode = true;
            Sleep(2000);
            continue;
        }

        std::cout << "[LAUNCHER] Da tha xich cho De (PID: " << pi.dwProcessId << "). Dang ngoi canh..." << std::endl;

        HANDLE waitHandles[] = { pi.hProcess, g_ServiceStopEvent };
        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0 + 1) {
            std::cout << "[LAUNCHER] Nhan lenh Stop tu he thong, dang cat co De de thoai vi..." << std::endl;
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            break;
        }

        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // ==================================================================
        // [FIX CHÍNH] 0xC000013A (3221225786) LÀ MÃ TỬ HÌNH DO TẮT CONSOLE!
        // ==================================================================
        if (g_IsShuttingDown || exitCode == 0xC000013A) {
            std::cout << "[LAUNCHER] He thong bi tat Console (Exit Code: " << exitCode << "). Bo qua kiem tra Crash!" << std::endl;
            break; // Thoát hẳn vòng lặp luôn, không Rollback, không đổi Slot!
        }

        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();

        std::cout << "[LAUNCHER] De da tat! Exit Code: " << exitCode << " | Thoi gian song: " << duration << " giay." << std::endl;

        if (exitCode == EXIT_CODE_SWITCH_SLOT) {
            std::cout << "[LAUNCHER] Nhan mat thu: De da Update than xac xong! Dang doi sang Slot moi..." << std::endl;
            Sleep(1000);
            continue;
        }

        if (exitCode != 0 && exitCode != EXIT_CODE_SWITCH_SLOT) {
            std::cout << "[!!!] BAO DONG: Ban Update bi loi Crash! Kich hoat HOAN LUONG!" << std::endl;
            std::string fallbackSlot = (currentSlot == "A") ? "B" : "A";
            std::cout << "[LAUNCHER] Dang quay xe tra ve Slot: " << fallbackSlot << std::endl;
            WriteSlot(fallbackSlot);
            isRecoveryMode = true;
            Sleep(2000);
            continue;
        }

        std::cout << "[LAUNCHER] He thong an toan. 3 giay nua khoi dong lai De..." << std::endl;
        Sleep(3000);
    }
    return ERROR_SUCCESS;
}

// =====================================================================
// BỘ ĐIỀU KHIỂN CHẾ ĐỘ SERVICE
// =====================================================================
VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    if (CtrlCode == SERVICE_CONTROL_STOP) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        SetEvent(g_ServiceStopEvent);
    }
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    g_StatusHandle = RegisterServiceCtrlHandlerA(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

// =====================================================================
// BẮT SỰ KIỆN KHI NGƯỜI DÙNG TẮT CỬA SỔ CONSOLE (Chữ X màu đỏ)
// =====================================================================
BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
        g_IsShuttingDown = true;
        std::cout << "\n[LAUNCHER] Nhan lenh tat Console, dang don dep..." << std::endl;
        SetEvent(g_ServiceStopEvent);
        Sleep(2000);
        return TRUE;
    }
    return FALSE;
}

// =====================================================================
// HÀM MAIN (Ngã 3 đường: Service hay Console?)
// =====================================================================
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    SERVICE_TABLE_ENTRYA ServiceTable[] = {
        {(LPSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONA)ServiceMain},
        {NULL, NULL}
    };

    if (StartServiceCtrlDispatcherA(ServiceTable) == FALSE) {
        DWORD err = GetLastError();

        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            std::cout << "[!] Phat hien ban dang chay file .exe thu cong (Debug Mode)." << std::endl;
            std::cout << "[*] He thong tu dong chuyen sang che do Console App..." << std::endl;

            SetConsoleCtrlHandler(ConsoleHandler, TRUE);
            g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

            ServiceWorkerThread(NULL);
        }
        else {
            std::cerr << "[-] Loi StartServiceCtrlDispatcher: " << err << std::endl;
            return err;
        }
    }

    return 0;
}