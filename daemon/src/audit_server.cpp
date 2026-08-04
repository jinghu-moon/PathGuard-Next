#include "pathguard/audit_server.h"

#include <cerrno>
#include <cstring>
#include <filesystem>

#if defined(__linux__)
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace pathguard::daemon {

AuditServer::AuditServer(std::string socket_path, std::string journal_path)
    : socket_path_(std::move(socket_path)),
      journal_(std::move(journal_path)),
      store_(&journal_),
      broker_(&store_) {}

AuditServer::~AuditServer() { Stop(); }

bool AuditServer::Start() {
#if !defined(__linux__)
    return false;
#else
    if (listen_fd_ >= 0 || socket_path_.empty()
        || socket_path_.size() >= sizeof(sockaddr_un::sun_path)
        || store_.Recover() != audit::Error::kNone) {
        return false;
    }
    std::error_code ignored;
    std::filesystem::remove(socket_path_, ignored);
    listen_fd_ = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path_.c_str(), socket_path_.size() + 1);
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
        || chmod(socket_path_.c_str(), 0600) != 0
        || listen(listen_fd_, 16) != 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        std::filesystem::remove(socket_path_, ignored);
        return false;
    }
    stopping_.store(false, std::memory_order_release);
    worker_ = std::thread(&AuditServer::Run, this);
    return true;
#endif
}

void AuditServer::Stop() {
#if defined(__linux__)
    stopping_.store(true, std::memory_order_release);
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (worker_.joinable()) worker_.join();
    std::error_code ignored;
    std::filesystem::remove(socket_path_, ignored);
#endif
}

void AuditServer::Run() {
#if defined(__linux__)
    while (!stopping_.load(std::memory_order_acquire)) {
        const int client = accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) continue;
            if (stopping_.load(std::memory_order_acquire)) break;
            continue;
        }
        ucred credentials{};
        socklen_t credential_size = sizeof(credentials);
        audit_protocol::Request request;
        audit_protocol::Response response;
        const bool trusted = getsockopt(client, SOL_SOCKET, SO_PEERCRED,
                                        &credentials, &credential_size) == 0
            && credentials.uid == 0;
        const ssize_t received = trusted
            ? recv(client, &request, sizeof(request), MSG_WAITALL) : -1;
        if (received == static_cast<ssize_t>(sizeof(request))
            && broker_.Handle(request, &response)) {
            send(client, &response, sizeof(response), MSG_NOSIGNAL);
        }
        close(client);
    }
#endif
}

}  // namespace pathguard::daemon
