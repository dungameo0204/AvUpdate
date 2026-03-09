#include <iostream>
#include "IpcServer.h"

int main() {
    std::cout << "=== NCS AV UPDATER DAEMON (Gia lap Service) ===" << std::endl;

    // Gọi hàm này là nó kẹt ở vòng lặp while(true) vô tận, liên tục lắng nghe
    IpcServer::StartListening();

    return 0;
}