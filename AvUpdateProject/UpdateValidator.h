#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <filesystem>
#include <iostream>
#include <sstream>

#pragma comment(lib, "advapi32.lib") // Thư viện để băm MD5 của Windows

namespace fs = std::filesystem;

namespace UpdateValidator {

    // =========================================================================
    // 2.1. SOÁT VÉ: NHÌN CỜ MUTEX ĐỂ BIẾT CHÍNH XÁC AV CÓ ĐANG QUÉT HAY KHÔNG
    // =========================================================================
    inline bool IsAvScanning() {
        // Đứng từ xa ngó xem có cái cờ nào tên là "AvActiveScanMutex" đang cắm không
        // (Dùng quyền SYNCHRONIZE là đủ để ngó, không cần cấp quyền sửa đổi)
        HANDLE hMutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\AvActiveScanMutex");

        if (hMutex != NULL) {
            // THẤY CỜ! -> Service đang bận quét thật sự.
            CloseHandle(hMutex); // Ngó xong thì bỏ tay ra để OS không bị rò rỉ bộ nhớ
            return true;
        }

        // KHÔNG THẤY CỜ! -> Service đang rảnh rỗi, hoặc vừa bị Cancel, hoặc đang tắt.
        return false;
    }
    // 2.3.1. So sánh Version (Trả về true nếu bản Mới > bản Cũ)
    inline bool IsNewerVersion(const std::string& newVer, const std::string& oldVer) {
        // Thuật toán so sánh chuỗi version (VD: 2.0.0 > 1.9.9) đơn giản:
        return newVer > oldVer;
    }

    // 2.3.2. Kiểm tra hạn sử dụng (Expires_at)
    inline bool IsExpired(long long expires_at) {
        long long current_time = std::time(nullptr);
        return current_time > expires_at;
    }

    // 2.3.3. Kiểm tra dung lượng đĩa cứng an toàn: (Tổng Size * 2) + 10MB
    inline bool CheckDiskSpace(const std::string& drivePath, uint64_t totalDownloadSize) {
        ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
        if (GetDiskFreeSpaceExA(drivePath.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
            uint64_t safeRequired = (totalDownloadSize * 2) + (10 * 1024 * 1024); // x2 + 10MB
            return freeBytesAvailable.QuadPart >= safeRequired;
        }
        return false;
    }

    // Tiện ích: Tính mã băm SHA-256 của một file trên ổ cứng siêu tốc
    inline std::string CalculateSHA256(const std::string& filepath) {
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        std::string sha256Str = "";

        // Dùng PROV_RSA_AES để hỗ trợ thuật toán SHA-256 trên Windows
        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return "";

        // Khởi tạo thuật toán CALG_SHA_256
        if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            CryptReleaseContext(hProv, 0);
            return "";
        }

        HANDLE hFile = CreateFileA(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            BYTE buffer[8192]; // Đọc từng cục 8KB để không tốn RAM
            DWORD bytesRead = 0;
            while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
                CryptHashData(hHash, buffer, bytesRead, 0);
            }
            CloseHandle(hFile);

            BYTE hash[32]; // SHA-256 dài 32 bytes (256 bits)
            DWORD hashLen = 32;
            if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
                char hex[65]; // 64 ký tự Hex + 1 ký tự null kết thúc chuỗi
                for (int i = 0; i < 32; i++) {
                    sprintf_s(&hex[i * 2], 3, "%02x", hash[i]);
                }
                sha256Str = std::string(hex);
            }
        }
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return sha256Str;
    }
}