#include "ServiceKiller.h"
#include <windows.h>
#include <iostream>

namespace ServiceKiller {

    bool StopServiceTask(const std::wstring& serviceName) {
        std::cout << "\n[*] Dang yeu cau STOP service..." << std::endl;

        SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
        if (!hSCManager) {
            std::cerr << "[-] Loi OpenSCManager! Thieu quyen Admin." << std::endl;
            return false;
        }

        SC_HANDLE hService = OpenService(hSCManager, serviceName.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (!hService) {
            std::cerr << "[-] Khong tim thay Service." << std::endl;
            CloseServiceHandle(hSCManager);
            return false;
        }

        SERVICE_STATUS_PROCESS ssp;
        DWORD bytesNeeded;

        if (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(SERVICE_STATUS_PROCESS), &bytesNeeded)) {
            if (ssp.dwCurrentState == SERVICE_STOPPED) {
                std::cout << "[+] Service da dung tu truoc roi, qua luon buoc tiep theo!" << std::endl;
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCManager);
                return true;
            }
        }

        SERVICE_STATUS status;
        if (!ControlService(hService, SERVICE_CONTROL_STOP, &status)) {
            DWORD err = GetLastError();

            if (err == ERROR_SERVICE_NOT_ACTIVE) {
                std::cout << "[+] Service da dung (Not Active), qua luon buoc tiep theo!" << std::endl;
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCManager);
                return true;
            }
            else if (err == ERROR_SERVICE_CANNOT_ACCEPT_CTRL) {
                std::cout << "[*] Service dang trong qua trinh dung (Stop Pending)..." << std::endl;
            }
            // === [BẢN VÁ MỚI] XỬ LÝ LỖI 1053 ===
            else if (err == 1053) {
                std::cerr << "[!] Canh bao: Service tat dot ngot (Loi 1053). Van tiep tuc kiem tra xac..." << std::endl;
                // KHÔNG return false ở đây, để luồng code rớt xuống vòng lặp check bên dưới!
            }
            // ====================================
            else {
                std::cerr << "[-] Khong the gui lenh Stop. Ma loi: " << err << std::endl;
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCManager);
                return false;
            }
        }

        std::cout << "[*] Dang cho service dung han..." << std::endl;
        while (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(SERVICE_STATUS_PROCESS), &bytesNeeded)) {
            if (ssp.dwCurrentState == SERVICE_STOPPED) {
                std::cout << "[+] Da tieu diet thanh cong!" << std::endl;
                break;
            }
            Sleep(500);
        }

        CloseServiceHandle(hService);
        CloseServiceHandle(hSCManager);
        return true;
    }

    bool StartServiceTask(const std::wstring& serviceName) {
        std::cout << "\n[+] Dang kich tim START service..." << std::endl;

        SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
        if (!hSCManager) return false;

        SC_HANDLE hService = OpenService(hSCManager, serviceName.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
        if (!hService) {
            CloseServiceHandle(hSCManager);
            return false;
        }

        if (!StartService(hService, 0, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_SERVICE_ALREADY_RUNNING) {
                std::cout << "[+] Service da chay san roi!" << std::endl;
            }
            else {
                std::cerr << "[-] Loi StartService. Ma loi: " << err << std::endl;
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCManager);
                return false;
            }
        }
        else {
            std::cout << "[+] Da goi hon thanh cong! Service dang chay lai." << std::endl;
        }

        CloseServiceHandle(hService);
        CloseServiceHandle(hSCManager);
        return true;
    }
}