#define NOMINMAX // Bùa chú chống lỗi min() của Windows
#include "AsyncEngine.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <set>       
#include <algorithm> 
#include <vector>
#include "Downloader.h"
#include "ServiceKiller.h"
#include "FileManipulator.h"
#include "UpdateValidator.h"
#include "UpdateState.h"

namespace fs = std::filesystem;

namespace AsyncEngine {
    // =====================================================================
    // [TUYỆT CHIÊU TỐI THƯỢNG] LẤY ĐƯỜNG DẪN TỪ CHÍNH WINDOWS SERVICE
    // =====================================================================
    std::string GetInstallDirFromService() {
        HKEY hKey;
        // Chọc thẳng vào hồ sơ quản lý Service của Windows
        LONG status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\AvScanVirus", 0, KEY_READ, &hKey);

        if (status != ERROR_SUCCESS) {
            std::cerr << "[-] Khong tim thay Service AvScanVirus! Dung duong dan mac dinh." << std::endl;
            return "D:\\ProjectTraining\\AvScanVirus";
        }

        char value[MAX_PATH];
        DWORD value_length = MAX_PATH;
        DWORD type;

        // Đọc cột ImagePath (nơi chứa đường dẫn file .exe)
        status = RegQueryValueExA(hKey, "ImagePath", NULL, &type, (LPBYTE)&value, &value_length);
        RegCloseKey(hKey);

        if (status != ERROR_SUCCESS) {
            std::cerr << "[-] Khong the doc ImagePath cua Service!" << std::endl;
            return "D:\\ProjectTraining\\AvScanVirus";
        }

        std::string exePath(value);

        // Ma thuật dọn rác: Xóa dấu ngoặc kép (") ở đầu và cuối do lệnh sc create sinh ra
        if (!exePath.empty() && exePath.front() == '"') {
            exePath.erase(0, 1);
        }
        if (!exePath.empty() && exePath.back() == '"') {
            exePath.pop_back();
        }

        // exePath đang là: D:\ProjectTraining\AvScanVirus\AvScanVirus.exe
        // Ta dùng fs::path để cắt lấy thư mục cha: D:\ProjectTraining\AvScanVirus
        try {
            return fs::path(exePath).parent_path().string();
        }
        catch (...) {
            return "D:\\ProjectTraining\\AvScanVirus";
        }
    }

    // =====================================================================
    // [HÀM BỘ Y TẾ] KHÔI PHỤC TỪ CÕI CHẾT (ROLLBACK UPDATE)
    // =====================================================================
    bool RollbackUpdate(const std::string& targetInstallDir) {
        std::string backupRootDir = targetInstallDir + "\\backups";
        std::string latestBackup = "";
        fs::file_time_type latestTime = (fs::file_time_type::min)();

        if (fs::exists(backupRootDir)) {
            for (const auto& entry : fs::directory_iterator(backupRootDir)) {
                if (entry.is_directory()) {
                    auto ftime = fs::last_write_time(entry);
                    if (ftime > latestTime) {
                        latestTime = ftime;
                        latestBackup = entry.path().string();
                    }
                }
            }
        }

        if (latestBackup.empty()) return false;

        std::cout << "\n[!!!] CANH BAO: Kich hoat quy trinh ROLLBACK..." << std::endl;
        std::cout << "[*] Dang lay lai file tu: " << latestBackup << std::endl;

        try {
            // 1. Phục hồi file cũ (Không chép đè thẳng mấy file text quản lý ra ngoài)
            for (auto it = fs::recursive_directory_iterator(latestBackup); it != fs::recursive_directory_iterator(); ++it) {
                if (it->is_regular_file()) {
                    std::string relPath = fs::relative(it->path(), latestBackup).string();
                    if (relPath == "snapshot.txt" || relPath == "version.txt") continue;

                    fs::path targetFile = fs::path(targetInstallDir) / relPath;
                    fs::create_directories(targetFile.parent_path());
                    fs::copy_file(it->path(), targetFile, fs::copy_options::overwrite_existing);
                    std::cout << "  -> Da cap cuu xong: " << relPath << std::endl;
                }
            }

            // 2. Phục hồi cái mác version.txt
            if (fs::exists(latestBackup + "\\version.txt")) {
                fs::copy_file(latestBackup + "\\version.txt", targetInstallDir + "\\version.txt", fs::copy_options::overwrite_existing);
            }

            // =================================================================
            // 3. MA THUẬT QUÉT RÁC DỰA TRÊN ẢNH CHỤP (SNAPSHOT)
            // =================================================================
            std::set<std::string> validOldFiles;
            std::ifstream snapFile(latestBackup + "\\snapshot.txt");
            if (snapFile.is_open()) {
                std::string line;
                while (std::getline(snapFile, line)) {
                    if (!line.empty()) validOldFiles.insert(line);
                }
                snapFile.close();

                validOldFiles.insert("version.txt");
                validOldFiles.insert("config.json");

                int deletedNewFiles = 0;
                for (auto it = fs::recursive_directory_iterator(targetInstallDir); it != fs::recursive_directory_iterator(); ++it) {
                    if (it->is_regular_file()) {
                        std::string localRelPath = fs::relative(it->path(), targetInstallDir).string();
                        std::replace(localRelPath.begin(), localRelPath.end(), '/', '\\');
                        std::transform(localRelPath.begin(), localRelPath.end(), localRelPath.begin(), ::tolower);

                        if (localRelPath.find("backups\\") == 0 || localRelPath.find("logs\\") == 0) continue;

                        // Chém ngay những file mới thêm vào ở bản update (không có trong snapshot)
                        if (validOldFiles.find(localRelPath) == validOldFiles.end()) {
                            fs::remove(it->path());
                            deletedNewFiles++;
                            std::cout << "  [-] Da xoa file dư thua (chi co o ban moi): " << localRelPath << std::endl;
                        }
                    }
                }
                if (deletedNewFiles > 0) std::cout << "[+] Da don dep " << deletedNewFiles << " file dư thua cua ban moi!" << std::endl;
            }
            // =================================================================

            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[-] Loi Rollback: " << e.what() << std::endl;
            return false;
        }
    }

    // =====================================================================
    // [HÀM CHÍNH] TIẾN TRÌNH CẬP NHẬT NGẦM
    // =====================================================================
    void BackgroundUpdateTask(models::UpdateCheckResponse remoteData, ConfigManager::AppConfig cfg) {
        UpdateState::IS_UPDATING = true;
        UpdateState::SetStatus(0.0, "Dang phan tich su khac biet (Diffing)...");

        try {
            std::string targetInstallDir = GetInstallDirFromService();
            std::cout << "[*] Duong dan cai dat (Tu Service): " << targetInstallDir << std::endl;
            // Dùng luôn targetInstallDir làm gốc thay vì app_root_dir
            fs::path fullDownloadDir = fs::path(targetInstallDir) / cfg.download_folder;
            fs::create_directories(fullDownloadDir);

            std::vector<std::string> downloadedFiles;
            uint64_t totalSizeToDownload = 0;
            std::vector<models::FileInfo> filesToDownload;

            for (const auto& remoteFile : remoteData.manifest.files) {
                std::string localFilePath = targetInstallDir + "\\" + remoteFile.path;
                bool needsDownload = false;

                if (!fs::exists(localFilePath)) needsDownload = true;
                else {
                    std::string localHash = UpdateValidator::CalculateSHA256(localFilePath);
                    if (localHash != remoteFile.md5) needsDownload = true;
                }

                if (needsDownload) {
                    filesToDownload.push_back(remoteFile);
                    totalSizeToDownload += remoteFile.size;
                }
            }

            if (filesToDownload.empty()) {
                UpdateState::SetStatus(100.0, "He thong da o phien ban moi nhat!");
                UpdateState::IS_UPDATING = false;
                return;
            }

            if (!UpdateValidator::CheckDiskSpace(fullDownloadDir.string(), totalSizeToDownload)) {
                UpdateState::SetStatus(0.0, "Loi: Khong du dung luong o dia!");
                UpdateState::IS_UPDATING = false;
                return;
            }

            int count = 0;
            int totalFiles = filesToDownload.size();

            for (const auto& file : filesToDownload) {
                double currentProgress = ((double)count / totalFiles) * 90.0;
                UpdateState::SetStatus(currentProgress, "Dang tai: " + file.path);

                std::string fullUrlPath = cfg.download_base_path + file.path;
                std::wstring fileDownloadPath = ConfigManager::ConvertToWString(fullUrlPath);
                std::string shortHash = file.md5.substr(0, 8);
                fs::path originalPath(file.path);
                std::string safeTempName = originalPath.stem().string() + "_" + shortHash + originalPath.extension().string();
                std::string saveTempLocation = (fullDownloadDir / safeTempName).string();

                fs::create_directories(fs::path(saveTempLocation).parent_path());

                if (Downloader::DownloadFile(cfg.server_domain, fileDownloadPath, saveTempLocation)) {
                    std::string downloadedHash = UpdateValidator::CalculateSHA256(saveTempLocation);
                    if (downloadedHash == file.md5) {
                        std::string finalLocation = (fullDownloadDir / file.path).string();
                        fs::create_directories(fs::path(finalLocation).parent_path());
                        if (fs::exists(finalLocation)) fs::remove(finalLocation);
                        fs::rename(saveTempLocation, finalLocation);
                        downloadedFiles.push_back(file.path);
                    }
                    else {
                        fs::remove(saveTempLocation);
                        UpdateState::SetStatus(currentProgress, "Loi: File bi hong (Sai Hash)!");
                        UpdateState::IS_UPDATING = false;
                        return;
                    }
                }
                else {
                    UpdateState::SetStatus(currentProgress, "Loi: Khong the ket noi mang!");
                    UpdateState::IS_UPDATING = false;
                    return;
                }
                count++;
            }

            UpdateState::SetStatus(95.0, "Tien hanh phau thuat chep de file...");

            // =================================================================
            // [TRICK MỚI] LƯU VERSION VÀ CHỤP ẢNH SNAPSHOT TRƯỚC KHI TẮT SERVICE
            // =================================================================
            std::string oldVersion = "Unknown";
            std::ifstream readVer(targetInstallDir + "\\version.txt");
            if (readVer.is_open()) {
                std::getline(readVer, oldVersion);
                readVer.close();
            }

            std::vector<std::string> oldFilesSnapshot;
            for (auto it = fs::recursive_directory_iterator(targetInstallDir); it != fs::recursive_directory_iterator(); ++it) {
                if (it->is_regular_file()) {
                    std::string localRelPath = fs::relative(it->path(), targetInstallDir).string();
                    std::replace(localRelPath.begin(), localRelPath.end(), '/', '\\');
                    std::transform(localRelPath.begin(), localRelPath.end(), localRelPath.begin(), ::tolower);
                    if (localRelPath.find("backups\\") != 0 && localRelPath.find("logs\\") != 0) {
                        oldFilesSnapshot.push_back(localRelPath);
                    }
                }
            }
            // =================================================================

            if (ServiceKiller::StopServiceTask(L"AvScanVirus")) {
                bool replaceSuccess = FileManipulator::BackupAndReplace(fullDownloadDir.string(), targetInstallDir, downloadedFiles);
                if (replaceSuccess) {
                    UpdateState::SetStatus(96.0, "Dang khoi dong lai Antivirus Service...");

                    std::string backupRootDir = targetInstallDir + "\\backups";
                    std::string latestBackup = "";
                    fs::file_time_type latestTime = (fs::file_time_type::min)();
                    try {
                        if (fs::exists(backupRootDir)) {
                            for (const auto& entry : fs::directory_iterator(backupRootDir)) {
                                if (entry.is_directory()) {
                                    auto ftime = fs::last_write_time(entry);
                                    if (ftime > latestTime) {
                                        latestTime = ftime;
                                        latestBackup = entry.path().string();
                                    }
                                }
                            }
                        }

                        // Nhét tờ giấy Snapshot và Version cũ vào Backup
                        if (!latestBackup.empty()) {
                            std::ofstream backupVer(latestBackup + "\\version.txt");
                            if (backupVer.is_open()) {
                                backupVer << oldVersion;
                                backupVer.close();
                            }
                            std::ofstream snapFile(latestBackup + "\\snapshot.txt");
                            if (snapFile.is_open()) {
                                for (const auto& f : oldFilesSnapshot) snapFile << f << "\n";
                                snapFile.close();
                            }
                        }
                    }
                    catch (...) {}

                    bool isServiceAlive = false;
                    try { isServiceAlive = ServiceKiller::StartServiceTask(L"AvScanVirus"); }
                    catch (...) {}

                    if (isServiceAlive) {
                        UpdateState::SetStatus(98.0, "Dang don dep cac file rac phien ban cu...");
                        try {
                            std::set<std::string> validFiles;
                            for (const auto& f : remoteData.manifest.files) {
                                std::string normalizedPath = f.path;
                                std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');
                                std::transform(normalizedPath.begin(), normalizedPath.end(), normalizedPath.begin(), ::tolower);
                                validFiles.insert(normalizedPath);
                            }
                            validFiles.insert("version.txt");
                            validFiles.insert("config.json");

                            std::vector<fs::path> trashFiles;
                            for (auto it = fs::recursive_directory_iterator(targetInstallDir); it != fs::recursive_directory_iterator(); ++it) {
                                if (it->is_regular_file()) {
                                    std::string localRelPath = fs::relative(it->path(), targetInstallDir).string();
                                    std::replace(localRelPath.begin(), localRelPath.end(), '/', '\\');
                                    std::transform(localRelPath.begin(), localRelPath.end(), localRelPath.begin(), ::tolower);

                                    if (localRelPath.find("backups\\") == 0 || localRelPath.find("logs\\") == 0) continue;

                                    if (validFiles.find(localRelPath) == validFiles.end()) {
                                        trashFiles.push_back(it->path());
                                    }
                                }
                            }

                            for (const auto& trashPath : trashFiles) {
                                std::string relPath = fs::relative(trashPath, targetInstallDir).string();
                                if (!latestBackup.empty()) {
                                    fs::path backupFilePath = fs::path(latestBackup) / relPath;
                                    fs::create_directories(backupFilePath.parent_path());
                                    fs::rename(trashPath, backupFilePath);
                                }
                                else {
                                    fs::remove(trashPath);
                                }
                            }

                            std::vector<fs::directory_entry> backups;
                            if (fs::exists(backupRootDir)) {
                                for (const auto& entry : fs::directory_iterator(backupRootDir)) {
                                    if (entry.is_directory()) backups.push_back(entry);
                                }
                                std::sort(backups.begin(), backups.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                                    return fs::last_write_time(a) > fs::last_write_time(b);
                                    });
                                for (size_t i = 1; i < backups.size(); ++i) {
                                    fs::remove_all(backups[i].path());
                                }
                            }
                        }
                        catch (...) {}

                        std::ofstream verFile(targetInstallDir + "\\version.txt");
                        if (verFile.is_open()) {
                            verFile << remoteData.manifest.version;
                            verFile.close();
                        }

                        UpdateState::SetStatus(100.0, "Cap nhat thanh cong!");
                    }
                    else {
                        UpdateState::SetStatus(96.0, "Loi Service: Kich hoat quy trinh Rollback...");
                        if (RollbackUpdate(targetInstallDir)) {
                            try { ServiceKiller::StartServiceTask(L"AvScanVirus"); }
                            catch (...) {}
                            UpdateState::SetStatus(0.0, "Ban cap nhat bi loi. He thong da an toan phuc hoi ban cu!");
                        }
                        else {
                            UpdateState::SetStatus(0.0, "Loi nghiem trong: Service crash va Rollback that bai!");
                        }
                    }
                }
                else {
                    UpdateState::SetStatus(95.0, "Loi: Khong the copy file!");
                    try { ServiceKiller::StartServiceTask(L"AvScanVirus"); }
                    catch (...) {}
                }
            }
            else {
                UpdateState::SetStatus(95.0, "Loi: Khong the dung Antivirus Service de cap nhat!");
            }

            UpdateState::IS_UPDATING = false;
        }
        catch (const std::exception& e) {
            UpdateState::SetStatus(0.0, std::string("Loi nghiem trong: ") + e.what());
            UpdateState::IS_UPDATING = false;
        }
        catch (...) {
            UpdateState::SetStatus(0.0, "Loi nghiem trong khong xac dinh!");
            UpdateState::IS_UPDATING = false;
        }
    }

    // =====================================================================
    // [HÀM MỚI] DÀNH CHO NÚT "HẠ CẤP BẢN CŨ" TỪ UI PYTHON
    // =====================================================================
    void ManualRollbackTask() {
        UpdateState::IS_UPDATING = true;
        ConfigManager::AppConfig cfg = ConfigManager::LoadConfig();
        std::string targetInstallDir = GetInstallDirFromService();
        std::string backupRootDir = targetInstallDir + "\\backups";
        std::string latestBackup = "";
        fs::file_time_type latestTime = (fs::file_time_type::min)();

        if (fs::exists(backupRootDir)) {
            for (const auto& entry : fs::directory_iterator(backupRootDir)) {
                if (entry.is_directory()) {
                    auto ftime = fs::last_write_time(entry);
                    if (ftime > latestTime) {
                        latestTime = ftime;
                        latestBackup = entry.path().string();
                    }
                }
            }
        }

        if (latestBackup.empty()) {
            UpdateState::SetStatus(0.0, "He thong dang o phien ban cu nhat, khong the ha cap them!");
            UpdateState::IS_UPDATING = false;
            return;
        }

        UpdateState::SetStatus(10.0, "Dang chuan bi khoi phuc ban cu...");

        if (ServiceKiller::StopServiceTask(L"AvScanVirus")) {
            UpdateState::SetStatus(40.0, "Dang xoa ban moi, chep de ban cu tu Backup...");

            if (RollbackUpdate(targetInstallDir)) {
                UpdateState::SetStatus(80.0, "Dang khoi dong lai Service...");
                try { ServiceKiller::StartServiceTask(L"AvScanVirus"); }
                catch (...) {}

                try { fs::remove_all(latestBackup); }
                catch (...) {}

                UpdateState::SetStatus(100.0, "Ha cap thanh cong! He thong da ve ban cu.");
            }
            else {
                UpdateState::SetStatus(0.0, "Loi: Khong tim thay ban Backup hoac phuc hoi that bai!");
                try { ServiceKiller::StartServiceTask(L"AvScanVirus"); }
                catch (...) {}
            }
        }
        else {
            UpdateState::SetStatus(0.0, "Loi: Khong the dung Service de Ha cap!");
        }
        UpdateState::IS_UPDATING = false;
    }
}