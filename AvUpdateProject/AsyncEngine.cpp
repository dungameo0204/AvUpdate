#include "AsyncEngine.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include "Downloader.h"
#include "ServiceKiller.h"
#include "FileManipulator.h"
#include "UpdateValidator.h"
#include "UpdateState.h"

namespace fs = std::filesystem;

namespace AsyncEngine {
    void BackgroundUpdateTask(models::UpdateCheckResponse remoteData, ConfigManager::AppConfig cfg) {
        // [TRẠNG THÁI] Bật cờ bắt đầu
        UpdateState::IS_UPDATING = true;
        UpdateState::SetStatus(0.0, "Dang phan tich su khac biet (Diffing)...");

        try {
            std::cout << "\n[ASYNC ENGINE] Dong co ngam da khoi dong! Dang Diffing..." << std::endl;
            std::string targetInstallDir = "D:\\ProjectTraining\\AvScanVirus";
            fs::path fullDownloadDir = fs::path(cfg.app_root_dir) / cfg.download_folder;
            fs::create_directories(fullDownloadDir);

            std::vector<std::string> downloadedFiles;
            uint64_t totalSizeToDownload = 0;
            std::vector<models::FileInfo> filesToDownload;

            // --- DIFFING ---
            for (const auto& remoteFile : remoteData.manifest.files) {
                std::string localFilePath = targetInstallDir + "\\" + remoteFile.path;
                bool needsDownload = false;

                if (!fs::exists(localFilePath)) {
                    needsDownload = true;
                }
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
                std::cout << "[ASYNC ENGINE] He thong da o phien ban moi nhat!" << std::endl;
                // [TRẠNG THÁI] Xong sớm nghỉ sớm
                UpdateState::SetStatus(100.0, "He thong da o phien ban moi nhat!");
                UpdateState::IS_UPDATING = false;
                return;
            }

            // --- CHECK Ổ CỨNG ---
            if (!UpdateValidator::CheckDiskSpace(fullDownloadDir.string(), totalSizeToDownload)) {
                std::cerr << "[-] Khong du dung luong o dia!" << std::endl;
                // [TRẠNG THÁI] Báo lỗi dung lượng
                UpdateState::SetStatus(0.0, "Loi: Khong du dung luong o dia!");
                UpdateState::IS_UPDATING = false;
                return;
            }

            // --- DOWNLOAD & VERIFY KÉP ---
            int count = 0;
            int totalFiles = filesToDownload.size();

            for (const auto& file : filesToDownload) {
                // [TRẠNG THÁI] Cập nhật % tiến độ (chiếm 90% tổng tiến trình)
                double currentProgress = ((double)count / totalFiles) * 90.0;
                UpdateState::SetStatus(currentProgress, "Dang tai: " + file.path);

                std::wstring fileDownloadPath = L"/dungameo0204/AvServer/main/AvServer/" + ConfigManager::ConvertToWString(file.path);

                std::string shortHash = file.md5.substr(0, 8);
                fs::path originalPath(file.path);
                std::string safeTempName = originalPath.stem().string() + "_" + shortHash + originalPath.extension().string();
                std::string saveTempLocation = (fullDownloadDir / safeTempName).string();

                std::cout << ">>> Dang tai ngam: " << file.path << " ... ";
                fs::create_directories(fs::path(saveTempLocation).parent_path());

                if (Downloader::DownloadFile(cfg.server_domain, fileDownloadPath, saveTempLocation)) {
                    std::string downloadedHash = UpdateValidator::CalculateSHA256(saveTempLocation);
                    if (downloadedHash == file.md5) {
                        std::cout << "XONG & KHOP HASH!" << std::endl;
                        std::string finalLocation = (fullDownloadDir / file.path).string();
                        fs::create_directories(fs::path(finalLocation).parent_path());
                        if (fs::exists(finalLocation)) fs::remove(finalLocation);
                        fs::rename(saveTempLocation, finalLocation);
                        downloadedFiles.push_back(file.path);
                    }
                    else {
                        std::cerr << "\n[-] LOI HASH! Huy tien trinh!" << std::endl;
                        fs::remove(saveTempLocation);
                        // [TRẠNG THÁI] Báo lỗi Hash
                        UpdateState::SetStatus(currentProgress, "Loi: File bi hong (Sai Hash)!");
                        UpdateState::IS_UPDATING = false;
                        return;
                    }
                }
                else {
                    std::cerr << "LOI MANG!" << std::endl;
                    // [TRẠNG THÁI] Báo lỗi Mạng
                    UpdateState::SetStatus(currentProgress, "Loi: Khong the ket noi mang!");
                    UpdateState::IS_UPDATING = false;
                    return;
                }
                count++;
            }

            // --- APPLY UPDATE ---
            // [TRẠNG THÁI] Đạt 95% - Tiến hành cài đặt
            UpdateState::SetStatus(95.0, "Tien hanh phau thuat chep de file...");
            std::cout << "\n[ASYNC ENGINE] Tien hanh Backup va Copy file..." << std::endl;

            if (ServiceKiller::StopServiceTask(L"AvScanVirus")) {
                bool replaceSuccess = FileManipulator::BackupAndReplace(fullDownloadDir.string(), targetInstallDir, downloadedFiles);
                if (replaceSuccess) {
                    std::cout << "[+] Phau thuat thanh cong!" << std::endl;

                    std::ofstream verFile(targetInstallDir + "\\version.txt");
                    if (verFile.is_open()) {
                        verFile << remoteData.manifest.version;
                        verFile.close();
                        std::cout << "[+] Da dong dau phien ban moi: " << remoteData.manifest.version << std::endl;
                    }

                    // [TRẠNG THÁI] THÀNH CÔNG RỰC RỠ 100%
                    UpdateState::SetStatus(100.0, "Cap nhat thanh cong!");
                }
                else {
                    std::cerr << "[-] Phau thuat that bai!" << std::endl;
                    // [TRẠNG THÁI] Báo lỗi Copy
                    UpdateState::SetStatus(95.0, "Loi: Khong the copy file!");
                }

                // ===== [BỌC THÉP NGHI PHẠM SỐ 1: Tránh chết lúc Start Service] =====
                try {
                    ServiceKiller::StartServiceTask(L"AvScanVirus");
                }
                catch (...) {
                    std::cerr << "[-] Loi vang ra tu StartServiceTask nhung da bi chan lai!" << std::endl;
                }
                // =================================================================
            }
            else {
                std::cerr << "[-] Khong the dung Service!" << std::endl;
                // [TRẠNG THÁI] Báo lỗi tắt Service
                UpdateState::SetStatus(95.0, "Loi: Khong the dung Antivirus Service!");
            }

            // Tắt cờ an toàn khi mọi việc xong xuôi
            UpdateState::IS_UPDATING = false;
        }
        catch (const std::exception& e) {
            std::cerr << "[-] LOI ASYNC WORKER: " << e.what() << std::endl;
            // [TRẠNG THÁI] Lỗi văng Exception
            UpdateState::SetStatus(0.0, std::string("Loi nghiem trong: ") + e.what());
            UpdateState::IS_UPDATING = false;
        }
        // ===== [BỌC THÉP NGHI PHẠM SỐ 2: Nuốt gọn mọi lỗi không xác định] =====
        catch (...) {
            std::cerr << "[-] LOI ASYNC WORKER KHONG XAC DINH!" << std::endl;
            UpdateState::SetStatus(0.0, "Loi nghiem trong khong xac dinh!");
            UpdateState::IS_UPDATING = false;
        }
        // ======================================================================
    }
}