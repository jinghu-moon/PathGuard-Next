#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "pathguard/audit_broker.h"

namespace pathguard::daemon {

class AuditServer final {
public:
    AuditServer(std::string socket_path, std::string journal_path);
    ~AuditServer();
    AuditServer(const AuditServer&) = delete;
    AuditServer& operator=(const AuditServer&) = delete;
    bool Start();
    void Stop();
private:
    void Run();
    std::string socket_path_;
    audit::FileJournal journal_;
    audit::Store store_;
    audit::Broker broker_;
    std::atomic<bool> stopping_{false};
    int listen_fd_ = -1;
    std::thread worker_;
};

}  // namespace pathguard::daemon
