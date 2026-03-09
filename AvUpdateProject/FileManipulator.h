#pragma once
#include <string>
#include <vector>

namespace FileManipulator {
    // Hàm thực hiện combo: Backup file cũ -> Chép đè file mới
    bool BackupAndReplace(const std::string& downloadDir, const std::string& targetDir, const std::vector<std::string>& fileNames);
}