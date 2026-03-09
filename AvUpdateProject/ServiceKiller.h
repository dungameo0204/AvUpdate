#pragma once
#include <string>

namespace ServiceKiller {
    // Hàm rút ống thở Service
    bool StopServiceTask(const std::wstring& serviceName);

    // Hàm kích tim Service đập lại
    bool StartServiceTask(const std::wstring& serviceName);
}