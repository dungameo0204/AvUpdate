#include <windows.h>
#include <iostream>
#include <thread>
#include "IpcServer.h"

// =========================================================================
// KHAI BÁO CÁC BIẾN TOÀN CỤC CỦA SERVICE
// =========================================================================
#define SERVICE_NAME "AvUpdaterService"

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;

VOID WINAPI ServiceMain(DWORD argc, LPSTR* argv);
VOID WINAPI ServiceCtrlHandler(DWORD request);
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam);

// =========================================================================
// 1. HÀM MAIN: CỬA NGÕ QUYẾT ĐỊNH SỐ PHẬN
// =========================================================================
int main() {
    SERVICE_TABLE_ENTRYA ServiceTable[] = {
        {(LPSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONA)ServiceMain},
        {NULL, NULL}
    };

    // Windows sẽ gọi hàm này để kết nối file exe vào hệ thống Service.
    // Nếu thất bại (do người dùng click đúp chuột chạy thủ công), nó sẽ văng ra.
    if (StartServiceCtrlDispatcherA(ServiceTable) == FALSE) {
        std::cout << "[!] Phat hien ban dang chay file .exe thu cong (Debug Mode)." << std::endl;
        std::cout << "[*] He thong tu dong chuyen sang che do Console App..." << std::endl;

        // Chạy trực tiếp Lễ tân không cần áo giáp Service (Dùng để Dev test)
        IpcServer::StartListening();
        return GetLastError();
    }

    return 0;
}

// =========================================================================
// 2. HÀM SERVICEMAIN: NƠI NHẬN LỆNH TỪ HỆ ĐIỀU HÀNH
// =========================================================================
VOID WINAPI ServiceMain(DWORD argc, LPSTR* argv) {
    DWORD Status = E_FAIL;

    // Đăng ký hàm Lắng nghe lệnh (Stop, Pause...) từ Windows
    g_StatusHandle = RegisterServiceCtrlHandlerA(SERVICE_NAME, ServiceCtrlHandler);
    if (g_StatusHandle == NULL) {
        return; // Đăng ký thất bại, tạch!
    }

    // Báo cáo cho Windows: "Tao đang khởi động nhé!"
    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // Tạo cái thẻ lệnh để biết lúc nào thì bị Sếp Windows đuổi việc (Stop)
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (g_ServiceStopEvent == NULL) {
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        g_ServiceStatus.dwCheckPoint = 1;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    // Báo cáo: "Tao đã khởi động xong, sẵn sàng nhận lệnh!"
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // Bắt đầu đẩy Lõi IPC của anh em mình ra chạy ở một Luồng ngầm
    HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

    // Đứng im tại đây chờ lệnh Rút ống thở từ Windows
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);

    // Nếu thoát khỏi WaitForSingleObject -> Có lệnh Stop -> Dọn dẹp đóng cửa sổ
    CloseHandle(g_ServiceStopEvent);
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 3;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

// =========================================================================
// 3. HÀM XỬ LÝ LỆNH TỪ BẢNG ĐIỀU KHIỂN WINDOWS (SERVICES.MSC)
// =========================================================================
VOID WINAPI ServiceCtrlHandler(DWORD request) {
    switch (request) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        // Sếp Windows hô "Dừng lại", mình báo cáo "Đang chuẩn bị dừng"
        g_ServiceStatus.dwWin32ExitCode = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        g_ServiceStatus.dwCheckPoint = 0;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

        // Bấm còi báo động cho hàm ServiceMain rút ống thở
        SetEvent(g_ServiceStopEvent);
        break;
    default:
        break;
    }
}

// =========================================================================
// 4. LUỒNG THỰC THI CHÍNH (RUỘT GAN CỦA ANH EM MÌNH NẰM Ở ĐÂY)
// =========================================================================
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam) {
    // Gọi anh Lễ tân Named Pipe ra đứng quầy 24/7 để chờ UI Python gõ cửa
    IpcServer::StartListening();
    return ERROR_SUCCESS;
}