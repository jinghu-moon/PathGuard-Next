#pragma once

#include "pathguard/provenance_protocol.h"
#include "pathguard/route_provenance.h"

namespace pathguard::provenance {

class ProvenanceBroker final {
public:
    explicit ProvenanceBroker(RouteProvenanceStore* store) : store_(store) {}
    bool Handle(const provenance_protocol::Request& request,
                provenance_protocol::Response* response);
private:
    RouteProvenanceStore* store_ = nullptr;
};

}  // namespace pathguard::provenance
