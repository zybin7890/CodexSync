#pragma once
#include "snapshot_job.hpp"
#include <map>
namespace cxs {
struct OperationRuntime;
class RestoreEngine {
public:
    explicit RestoreEngine(OperationRuntime& runtime):runtime_(runtime){}
    fs::path root_path(const std::string& id);
    void materialize(const Json& e,const fs::path& dest);
    void restore_progress(const Json& entries);
private:
    OperationRuntime& runtime_;
    void begin_progress(const std::string&,uint64_t,uint64_t,uint64_t) noexcept;
    void advance_progress(uint64_t,uint64_t,uint64_t,uint64_t transferred=0) noexcept;
    void phase_progress(const std::string&) noexcept;
    void checkpoint();
    std::string object(const Bytes&);
    Bytes fetch(const std::string&);
    fs::path cache(const std::string&);
};
}
