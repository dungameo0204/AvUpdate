#include "Downloader.h"
#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <fstream>
#include <filesystem>
// Báo cho Visual Studio tự động link thư viện WinHTTP lúc build
#pragma comment(lib, "winhttp.lib")

namespace Downloader {

    std::string FetchHTTP(const std::wstring& domain, const std::wstring& path) {
        std::string result = "";

        // Mở phiên làm việc
        HINTERNET hSession = WinHttpOpen(L"NCS_AVUpdater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            // Kết nối vào Domain (dungameo0204.github.io) qua cổng 443 (HTTPS)
            HINTERNET hConnect = WinHttpConnect(hSession, domain.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (hConnect) {
                // Tạo Request GET (Nhớ có cờ WINHTTP_FLAG_SECURE để chạy HTTPS)
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH);
                if (hRequest) {
                    // Gửi Request
                    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
                    if (bResults) bResults = WinHttpReceiveResponse(hRequest, NULL);

                    // Đọc dữ liệu trả về
                    if (bResults) {
                        DWORD dwSize = 0;
                        DWORD dwDownloaded = 0;
                        do {
                            dwSize = 0;
                            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                            if (dwSize == 0) break;

                            char* pszOutBuffer = new char[dwSize + 1];
                            ZeroMemory(pszOutBuffer, dwSize + 1);

                            if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                                result += pszOutBuffer; // Cộng dồn chuỗi JSON
                            }
                            delete[] pszOutBuffer; 
                        } while (dwSize > 0);
                    }
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
        }
        return result;
    }

    models::UpdateCheckResponse CheckUpdate(const std::wstring& domain, const std::wstring& path) {
        std::cout << "[*] Dang ket noi den Server..." << std::endl;
        std::string jsonString = FetchHTTP(domain, path);

        if (jsonString.empty()) {
            throw std::runtime_error("Khong the tai JSON tu Server! Check lai mang hoac URL.");
        }

        std::cout << "[*] Tai JSON thanh cong! Dang boc tach..." << std::endl;
       

        // THAY CHỮ 'path' THÀNH TÊN BIẾN CHỨA DỮ LIỆU TẢI VỀ CỦA BÁC (VD: responseData)
        std::cout << "[DEBUG] Ruot file JSON C++ tai ve duoc la:\n" << jsonString << "\n" << std::endl;
        // MA THUẬT CỦA C++ Ở ĐÂY: Ép chuỗi JSON vào Struct chỉ bằng 2 dòng!
        auto jsonParsed = json::parse(jsonString);
        return jsonParsed.get<models::UpdateCheckResponse>();
    }

    bool DownloadFile(const std::wstring& domain, const std::wstring& path, const std::string& savePath) {
        bool success = false;
        try {
            std::filesystem::path filePath(savePath);
            std::filesystem::create_directories(filePath.parent_path());
        }
        catch (...) {}
        // Mở file nhị phân để chuẩn bị ghi
        std::ofstream outFile(savePath, std::ios::binary);
        if (!outFile.is_open()) return false;

        HINTERNET hSession = WinHttpOpen(L"NCS_AVUpdater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            HINTERNET hConnect = WinHttpConnect(hSession, domain.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (hConnect) {
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH);
                if (hRequest) {
                    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                        if (WinHttpReceiveResponse(hRequest, NULL)) {
                            // ===== [BẮT ĐẦU CHÈN CODE CHECK LỖI HTTP 404] =====
                            DWORD dwStatusCode = 0;
                            DWORD dwSizeStatus = sizeof(dwStatusCode);
                            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSizeStatus, WINHTTP_NO_HEADER_INDEX);

                            if (dwStatusCode != 200) {
                                std::cerr << "[-] Tu choi tai! Server bao loi HTTP " << dwStatusCode << " voi file nay!" << std::endl;
                                outFile.close();
                                std::filesystem::remove(savePath); // Xóa ngay cái file HTML 9KB rác rưởi đi
                                WinHttpCloseHandle(hRequest);
                                WinHttpCloseHandle(hConnect);
                                WinHttpCloseHandle(hSession);
                                return false; // Dừng luôn, báo tải thất bại!
                            }
                            DWORD dwSize = 0;
                            DWORD dwDownloaded = 0;

                            // Vòng lặp: Mạng trả về cục byte nào, ta ghi thẳng cục byte đó vào file
                            do {
                                dwSize = 0;
                                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                                if (dwSize == 0) break;

                                char* buffer = new char[dwSize];
                                if (WinHttpReadData(hRequest, (LPVOID)buffer, dwSize, &dwDownloaded)) {
                                    outFile.write(buffer, dwDownloaded); // GHI RA Ổ CỨNG
                                }
                                delete[] buffer;
                            } while (dwSize > 0);

                            success = true; // Tải trót lọt
                        }
                    }
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
        }
        outFile.close();
        return success;
    }
}