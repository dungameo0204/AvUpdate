#pragma once
#include <string>
#include <vector>
#include "json.hpp" // Triệu hồi sức mạnh JSON

using json = nlohmann::json;

namespace models {
    struct FileInfo {
        std::string path;
        std::string url;
        std::string md5;
        long long size;
        json metadata; // Khai báo hẳn kiểu json luôn cho oai!
    };

    struct Manifest {
        std::string version;
        long long expires_at;
        std::vector<FileInfo> files;
    };

    struct UpdateCheckResponse {
        bool has_update;
        Manifest manifest;
    };

    // Định nghĩa macro thần thánh của nlohmann để nó TỰ ĐỘNG map JSON vào Struct C++
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FileInfo, path, url, md5, size, metadata)
        NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Manifest, version, expires_at, files)
        NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UpdateCheckResponse, has_update, manifest)

        // Struct dùng để báo cáo tiến độ qua Pipe
        struct StatusMessage {
        std::string message;
        std::string status;
        double process_download;
        long long timestamp;
        std::string version;
    };
}