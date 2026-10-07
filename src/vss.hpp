#pragma once
#include "core.hpp"
namespace cxs {
// Snapshot lifetime ends before network I/O. No elevated network process.
class VssSession {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    VssSession(const Json& config, const fs::path& state);
    ~VssSession();
    fs::path source(const fs::path& original) const;
    void finish();
};
}
