#define NOMINMAX // Bùa chú chống lỗi min() của Windows
#include "AsyncEngine.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <set>       
#include <algorithm> 
#include <vector>
#include <stdlib.h>
#include <windows.h> // Bổ sung thư viện này để xài hàm Sleep()
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
        LONG status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\AvScanVirus", 0, KEY_READ, &hKey);

        if (status != ERROR_SUCCESS) {
            return "D:\\ProjectTraining\\AvScanVirus"; // Trả về mặc định nếu không thấy
        }

        // ==========================================================
        // [FIX CHÍNH CỐT LÕI]: XÓA SẠCH BỘ NHỚ TRƯỚC KHI ĐỌC REGISTRY
        // ==========================================================
        char value[MAX_PATH];
        memset(value, 0, sizeof(value)); // Điền toàn bộ mảng bằng số 0 (NULL)

        DWORD value_length = MAX_PATH - 1; // Bớt lại 1 byte cuối cùng làm vách ngăn
        DWORD type;

        status = RegQueryValueExA(hKey, "ImagePath", NULL, &type, (LPBYTE)&value, &value_length);
        RegCloseKey(hKey);

        if (status != ERROR_SUCCESS) {
            return "D:\\ProjectTraining\\AvScanVirus";
        }

        // Ép buộc ký tự cuối cùng phải là NULL để std::string không bị tràn bộ nhớ
        value[MAX_PATH - 1] = '\0';

        std::string exePath(value);

        // Dọn rác dấu ngoặc kép
        if (!exePath.empty() && exePath.front() == '"') {
            exePath.erase(0, 1);
        }
        if (!exePath.empty() && exePath.back() == '"') {
            exePath.pop_back();
        }

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
            // 1. Phục hồi file cũ
            for (auto it = fs::recursive_directory_iterator(latestBackup); it != fs::recursive_directory_iterator(); ++it) {
                if (it->is_regular_file()) {
                    std::string relPath = fs::relative(it->path(), latestBackup).string();

                    std::string lowerRelPath = relPath;
                    std::transform(lowerRelPath.begin(), lowerRelPath.end(), lowerRelPath.begin(), ::tolower);

                    // BỎ QUA NHỮNG KẺ BẤT TỬ ĐỂ KHÔNG BỊ ACCESS DENIED
                    if (lowerRelPath == "snapshot.txt" ||
                        lowerRelPath == "version.txt" ||
                        lowerRelPath.find("applauncher.exe") != std::string::npos ||
                        lowerRelPath.find("slot_a\\") == 0 ||
                        lowerRelPath.find("slot_b\\") == 0) {
                        continue; // Bơ đi, không chép đè!
                    }

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

            // 3. MA THUẬT QUÉT RÁC DỰA TRÊN ẢNH CHỤP (SNAPSHOT)
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

                        if (validOldFiles.find(localRelPath) == validOldFiles.end()) {
                            fs::remove(it->path());
                            deletedNewFiles++;
                            std::cout << "  [-] Da xoa file du thua (chi co o ban moi): " << localRelPath << std::endl;
                        }
                    }
                }
                if (deletedNewFiles > 0) std::cout << "[+] Da don dep " << deletedNewFiles << " file du thua cua ban moi!" << std::endl;
            }

            return true; // <--- CÁI DÒNG NÀY ĐỊNH ĐOẠT SỰ SỐNG CÒN ĐÂY NÀY!
        }
        catch (const std::exception& e) {
            std::cerr << "[-] Loi Rollback: " << e.what() << std::endl;
            return false;
        }
    }

 
    // =====================================================================
    // [HÀM CHÍNH] TIẾN TRÌNH CẬP NHẬT NGẦM (ĐÃ TÍCH HỢP SELF-UPDATE TRÌ HOÃN)
    // =====================================================================
    void BackgroundUpdateTask(models::UpdateCheckResponse remoteData, ConfigManager::AppConfig cfg) {
        UpdateState::IS_UPDATING = true;
        UpdateState::SetStatus(0.0, "Dang phan tich su khac biet (Diffing)...");

        try {
            std::string targetInstallDir = GetInstallDirFromService();
            std::cout << "[*] Duong dan cai dat (Tu Service): " << targetInstallDir << std::endl;

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

            // QUÁ TRÌNH TẢI FILE
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
                    // ==============================================================
                    // [BẬT ĐÈN PHA CHÓI LÓA TÓM GỌN LỖI HASH]
                    // ==============================================================
                    std::cout << "\n[DEBUG HASH] Dang kiem tra file: " << file.path << std::endl;
                    std::cout << "   -> Hash file VUA TAI VE (SHA256): " << downloadedHash << std::endl;
                    std::cout << "   -> Hash tren SERVER JSON (md5):   " << file.md5 << std::endl;

                    // Ép cả 2 thằng về chữ thường (lowercase) để so sánh cho công bằng
                    std::string hash1 = downloadedHash;
                    std::string hash2 = file.md5;
                    std::transform(hash1.begin(), hash1.end(), hash1.begin(), ::tolower);
                    std::transform(hash2.begin(), hash2.end(), hash2.begin(), ::tolower);
                    if (hash1 == hash2) {
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

            // =================================================================
            // [MODULE 5] RADAR PHÁT HIỆN "TỰ LỘT XÁC" (SELF-UPDATE)
            // =================================================================
            bool isSelfUpdate = false;
            std::string newUpdaterPath = "";
            std::string updaterFileName = "";
            bool requireSlotSwitch = false;
            std::string nextSlotToSwitch = "";

            for (const auto& f : downloadedFiles) {
                std::string lowerPath = f;
                std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

                if (lowerPath.find("avupdater.exe") != std::string::npos ||
                    lowerPath.find("avupdateproject.exe") != std::string::npos) {
                    isSelfUpdate = true;
                    newUpdaterPath = (fullDownloadDir / f).string();
                    updaterFileName = fs::path(f).filename().string();
                    break;
                }
            }

            if (isSelfUpdate) {
                UpdateState::SetStatus(94.0, "Phat hien ban cap nhat Updater! Dang len lich lot xac...");
                std::cout << "[*] Kich hoat che do SELF-UPDATE!" << std::endl;

                std::string configPath = targetInstallDir + "\\slot_config.txt";
                std::string currentSlot = "A";
                std::ifstream inFile(configPath);
                if (inFile.is_open()) { std::getline(inFile, currentSlot); inFile.close(); }
                if (currentSlot != "A" && currentSlot != "B") currentSlot = "A";

                nextSlotToSwitch = (currentSlot == "A") ? "B" : "A";
                std::string nextSlotDir = targetInstallDir + "\\Slot_" + nextSlotToSwitch;
                fs::create_directories(nextSlotDir);

                // Copy đè file mới vào Slot nhà bên cạnh
                fs::copy_file(newUpdaterPath, nextSlotDir + "\\" + updaterFileName, fs::copy_options::overwrite_existing);

                // [FIX CHÍNH]: TUYỆT ĐỐI KHÔNG GHI SỔ NAM TÀO Ở ĐÂY!!! 
                // Lỡ tí nữa Antivirus hỏng thì sao? Chỉ cắm cờ chờ lệnh thôi!
                requireSlotSwitch = true;
                std::cout << "[UPDATER] Da copy xac sang Slot " << nextSlotToSwitch << ". Cho Service chay thanh cong moi chuyen ho khau!" << std::endl;
            }
            // =================================================================

            UpdateState::SetStatus(95.0, "Tien hanh phau thuat chep de file...");

            // LƯU VERSION VÀ CHỤP ẢNH SNAPSHOT TRƯỚC KHI TẮT SERVICE
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
            // [MODULE MỚI]: CHỜ ĐỢI AV QUÉT XONG & TỈA SÚNG UI DYNAMIC
            // =================================================================

            // 1. CHỜ ĐỢI SERVICE QUÉT XONG
            if (UpdateValidator::IsAvScanning()) {
                UpdateState::SetStatus(92.0, "He thong dang ban quet Virus. Vui long cho...");
                std::cout << "[*] AV dang ban quet. Dua vao trang thai CHO (Wait State)..." << std::endl;

                // Luồng ngầm nên ngủ bao lâu cũng được, Python UI không bị đơ!
                while (UpdateValidator::IsAvScanning()) {
                    Sleep(5000);
                }
                std::cout << "[+] AV da quet xong! Tiep tuc qua trinh Update..." << std::endl;
            }

            // 2. SÁT THỦ CÓ DANH SÁCH (Dynamic Kill UI đang mở)
            UpdateState::SetStatus(94.0, "Dang dong cac ung dung dang mo...");
            std::cout << "[*] Kich hoat che do TIA SUNG (Dynamic Kill)..." << std::endl;

            for (const auto& file : downloadedFiles) {
                fs::path p(file);
                if (p.extension() == ".exe") {
                    std::string exeName = p.filename().string();
                    std::string lowerExeName = exeName;
                    std::transform(lowerExeName.begin(), lowerExeName.end(), lowerExeName.begin(), ::tolower);

                    // Tuyệt đối không tự sát!
                    if (lowerExeName != "avupdater.exe" && lowerExeName != "avupdateproject.exe" && lowerExeName != "applauncher.exe") {
                        std::cout << "   -> Dang Kill tien trinh: " << exeName << std::endl;
                        std::string killCmd = "taskkill /F /IM " + exeName + " >nul 2>&1";
                        system(killCmd.c_str());
                    }
                }
            }
            Sleep(1500); // Đợi 1.5s cho UI sập hẳn

            UpdateState::SetStatus(95.0, "Tien hanh phau thuat chep de file...");
            // =================================================================

            // TẮT SERVICE VÀ ĐÈ FILE
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

                    // BẬT LẠI SERVICE
                    bool isServiceAlive = false;
                    try { isServiceAlive = ServiceKiller::StartServiceTask(L"AvScanVirus"); }
                    catch (...) {}

                    if (isServiceAlive) {
                        // ==============================================================
                        // KỊCH BẢN 1: BẢN CẬP NHẬT HOÀN HẢO (SERVICE SỐNG)
                        // ==============================================================
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
                            validFiles.insert("slot_config.txt");
                            validFiles.insert("applauncher.exe");

                            std::vector<fs::path> trashFiles;
                            for (auto it = fs::recursive_directory_iterator(targetInstallDir); it != fs::recursive_directory_iterator(); ++it) {
                                if (it->is_regular_file()) {
                                    std::string localRelPath = fs::relative(it->path(), targetInstallDir).string();
                                    std::replace(localRelPath.begin(), localRelPath.end(), '/', '\\');
                                    std::transform(localRelPath.begin(), localRelPath.end(), localRelPath.begin(), ::tolower);

                                    if (localRelPath.find("backups\\") == 0 ||
                                        localRelPath.find("logs\\") == 0 ||
                                        localRelPath.find("slot_a\\") == 0 ||
                                        localRelPath.find("slot_b\\") == 0) continue;

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

                        // BÁO CÁO THÀNH CÔNG CHO UI PYTHON
                        if (requireSlotSwitch) {
                            // [ĐÂY MỚI LÀ CHỖ ĐƯỢC PHÉP GHI SỔ NAM TÀO]
                            std::string configPath = targetInstallDir + "\\slot_config.txt";
                            std::ofstream outFile(configPath);
                            if (outFile.is_open()) { outFile << nextSlotToSwitch; outFile.close(); }

                            UpdateState::SetStatus(99.0, "Dang lot xac... Giao dien vui long doi va ket noi lai IPC!");
                            std::cout << "[UPDATER] Nhiem vu hoan tat! 2s nua tu sat de goi phien ban moi..." << std::endl;
                            Sleep(2000);
                            exit(99);
                        }
                        else {
                            UpdateState::SetStatus(100.0, "Cap nhat thanh cong!");
                        }
                    }
                    else {
                        // ==============================================================
                        // KỊCH BẢN 2: BẢN CẬP NHẬT BỊ LỖI (SERVICE CHẾT) -> TỰ ROLLBACK
                        // ==============================================================
                        std::cout << "[!!!] CANH BAO: Kich hoat quy trinh ROLLBACK do Service 216..." << std::endl;
                        UpdateState::SetStatus(96.0, "Loi Service: Kich hoat quy trinh Rollback...");
                        if (RollbackUpdate(targetInstallDir)) {
                            try { ServiceKiller::StartServiceTask(L"AvScanVirus"); }
                            catch (...) {}

                            // [FIX CHÍNH]: Ép gửi 100.0 và chữ [RECOVERY] để UI Python bật bảng đỏ chót!
                            UpdateState::SetStatus(100.0, "[RECOVERY] Phien ban moi bi loi khoi dong. He thong da an toan phuc hoi ban cu!");
                        }
                        else {
                            UpdateState::SetStatus(0.0, "Loi nghiem trong: Service crash va Rollback that bai!");
                        }
                        // Không được exit() ở đây, cứ để sống bình thường vì mình vừa tự chữa bệnh xong!
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
    // HÀM ROLLBACK THỦ CÔNG (ĐƯỢC GỌI TỪ NÚT BẤM TRÊN UI HOẶC LỆNH BÀI)
    // =====================================================================
    void ManualRollbackTask() {
        UpdateState::IS_UPDATING = true;
        UpdateState::SetStatus(10.0, "Dang kiem tra kho luu tru ban du phong...");

        try {
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
                UpdateState::SetStatus(99.0, "He thong dang o phien ban cu nhat, khong the ha cap them!");
                UpdateState::IS_UPDATING = false;
                return;
            }

            // =================================================================
            // [BỌC THÉP CHO ROLLBACK]: CHỜ ĐỢI AV QUÉT & TỈA SÚNG DYNAMIC
            // =================================================================

            // 1. CHỜ ĐỢI SERVICE QUÉT XONG (Bằng Radar Ống nước)
            if (UpdateValidator::IsAvScanning()) {
                UpdateState::SetStatus(30.0, "He thong dang ban quet Virus. Vui long cho...");
                std::cout << "[*] AV dang ban quet. Dua vao trang thai CHO Rollback..." << std::endl;

                while (UpdateValidator::IsAvScanning()) {
                    Sleep(5000);
                }
                std::cout << "[+] AV da quet xong! Tiep tuc qua trinh Rollback..." << std::endl;
            }

            // 2. SÁT THỦ ĐỌC DANH SÁCH TỪ THƯ MỤC BACKUP
            UpdateState::SetStatus(35.0, "Dang dong cac ung dung dang mo...");
            std::cout << "[*] Kich hoat che do TIA SUNG cho Rollback..." << std::endl;

            if (fs::exists(latestBackup)) {
                for (auto it = fs::recursive_directory_iterator(latestBackup); it != fs::recursive_directory_iterator(); ++it) {
                    if (it->is_regular_file() && it->path().extension() == ".exe") {
                        std::string exeName = it->path().filename().string();
                        std::string lowerExeName = exeName;
                        std::transform(lowerExeName.begin(), lowerExeName.end(), lowerExeName.begin(), ::tolower);

                        // Tha cho các công thần của hệ thống Update
                        if (lowerExeName != "avupdater.exe" && lowerExeName != "avupdateproject.exe" && lowerExeName != "applauncher.exe") {
                            std::cout << "   -> Dang Kill tien trinh UI: " << exeName << std::endl;
                            std::string killCmd = "taskkill /F /IM " + exeName + " >nul 2>&1";
                            system(killCmd.c_str());
                        }
                    }
                }
            }
            Sleep(1500); // Đợi 1.5 giây cho Windows thu dọn xác chết
            // =================================================================

            UpdateState::SetStatus(40.0, "Tim thay ban an toan, dang dung Service...");

            // TẮT SERVICE VÀ TIẾN HÀNH PHỤC HỒI
            if (ServiceKiller::StopServiceTask(L"AvScanVirus")) {

                std::cout << "[*] Dang cho Service nha file ra..." << std::endl;
                Sleep(2000);

                if (RollbackUpdate(targetInstallDir)) {
                    try {
                        std::cout << "[*] Da dung xong backup, tien hanh xoa: " << latestBackup << std::endl;
                        fs::remove_all(latestBackup);
                    }
                    catch (...) {}

                    UpdateState::SetStatus(80.0, "Dang khoi dong lai Service...");
                    try { ServiceKiller::StartServiceTask(L"AvScanVirus"); }
                    catch (...) {}

                    UpdateState::SetStatus(100.0, "[MANUAL_ROLLBACK] Ha cap thanh cong. He thong da an toan!");
                }
                else {
                    UpdateState::SetStatus(0.0, "Loi: Qua trinh Rollback that bai giua chung!");
                    try { ServiceKiller::StartServiceTask(L"AvScanVirus"); }
                    catch (...) {}
                }
            }
            else {
                UpdateState::SetStatus(0.0, "Loi: Khong the dung Antivirus Service de Rollback!");
            }
        }
        catch (...) {
            UpdateState::SetStatus(0.0, "Loi nghiem trong khi thuc hien quy trinh Ha cap!");
        }

        UpdateState::IS_UPDATING = false;
    }
}