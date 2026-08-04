#pragma once

#include "pathguard/audit_protocol.h"
#include "pathguard/route_audit.h"

namespace pathguard::audit {

class Broker final {
public:
    explicit Broker(Store* store) : store_(store) {}
    bool Handle(const audit_protocol::Request& request,
                audit_protocol::Response* response);
private:
    Store* store_ = nullptr;
};

}  // namespace pathguard::audit
