#include <cassert>
#include <string>

#include "hide_probe_contract.h"

int main() {
    using namespace pathguard::hide_probe;

    assert(EscapeJson("a\"b\\c\n\t") == "a\\\"b\\\\c\\n\\t");
    assert(EscapeJson(std::string("x\x01y", 3)) == "x\\u0001y");

    const Observation observation{
        .test = "openat_relative",
        .surface = "direct_vfs",
        .path = "Pictures/\"Secret\"",
        .return_value = -1,
        .error_number = 2,
        .side_effect = false,
        .status = ProbeStatus::kObserved,
    };
    assert(RenderObservationJson(observation) ==
           "{\"schema\":1,\"kind\":\"observation\","
           "\"test\":\"openat_relative\",\"surface\":\"direct_vfs\","
           "\"path\":\"Pictures/\\\"Secret\\\"\",\"return_value\":-1,"
           "\"errno\":2,\"side_effect\":false,\"status\":\"observed\"}");

    Observation unsupported = observation;
    unsupported.status = ProbeStatus::kUnsupported;
    assert(RenderObservationJson(unsupported).find(
               "\"status\":\"unsupported\"") != std::string::npos);

    Observation setup_error = observation;
    setup_error.status = ProbeStatus::kSetupError;
    assert(RenderObservationJson(setup_error).find(
               "\"status\":\"setup_error\"") != std::string::npos);

    assert(IsAllowedSandboxPath(
        "/data/local/tmp/pathguard-hide-h0-20260728-001"));
    assert(IsAllowedSandboxPath(
        "/data/user/0/dev.pathguard.hideprobe/no_backup/"
        "pathguard-hide-h0-native-20260728-001"));
    assert(IsAllowedSandboxPath(
        "/data/data/dev.pathguard.hideprobe/no_backup/"
        "pathguard-hide-h0-native-20260728-001"));
    assert(!IsAllowedSandboxPath("/data/local/tmp/pathguard-hide-h0-"));
    assert(!IsAllowedSandboxPath(
        "/data/local/tmp/pathguard-hide-h0-a/../outside"));
    assert(!IsAllowedSandboxPath(
        "/data/local/tmp/pathguard-hide-h0-a/nested"));
    assert(!IsAllowedSandboxPath("/data/local/tmp/other"));
    assert(!IsAllowedSandboxPath(
        "/data/user/0/other.package/no_backup/pathguard-hide-h0-a"));
    assert(!IsAllowedSandboxPath(
        "/data/user/0/dev.pathguard.hideprobe/files/pathguard-hide-h0-a"));
    assert(!IsAllowedSandboxPath(
        "/data/data/other.package/no_backup/pathguard-hide-h0-a"));
    assert(!IsAllowedSandboxPath(
        "/data/local/tmp/pathguard-hide-h0-a\nother"));
    return 0;
}
