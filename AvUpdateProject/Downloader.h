#pragma once
#include <string>
#include "models.h"

namespace Downloader {
    // Kéo JSON
    std::string FetchHTTP(const std::wstring& domain, const std::wstring& path);
    models::UpdateCheckResponse CheckUpdate(const std::wstring& domain, const std::wstring& path);

    // [MỚI] Hàm kéo file nhị phân (.exe, .dll) lưu ra ổ cứng
    bool DownloadFile(const std::wstring& domain, const std::wstring& path, const std::string& savePath);
}