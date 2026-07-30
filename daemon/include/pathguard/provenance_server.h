#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "pathguard/provenance_broker.h"

namespace pathguard::daemon {

class ProvenanceServer final {
public:
    ProvenanceServer(std::string socket_path, std::string journal_path);
    ~ProvenanceServer();
    ProvenanceServer(const ProvenanceServer&) = delete;
    ProvenanceServer& operator=(const ProvenanceServer&) = delete;
    bool Start();
    void Stop();
private:
    void Run();
    std::string socket_path_;
    provenance::FileRouteJournal journal_;
    provenance::RouteProvenanceStore store_;
    provenance::ProvenanceBroker broker_;
    std::atomic<bool> stopping_{false};
    int listen_fd_ = -1;
    std::thread worker_;
};

}  // namespace pathguard::daemon
