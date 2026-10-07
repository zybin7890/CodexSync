#pragma once
#include "snapshot_job.hpp"
#include <map>
namespace cxs {
struct OperationRuntime;
class CaptureEngine {
public:
    explicit CaptureEngine(OperationRuntime& runtime):runtime_(runtime){}
    Json entry(const fs::path& p,const std::string& root,const std::string& rel,bool scanning=false,bool shadow=false);
    Json scan();
    Json incremental_capture(ChangeCatalog& catalog,Json& next_catalog);
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
