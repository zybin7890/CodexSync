#pragma once
#include "snapshot_job.hpp"
#include <map>
namespace cxs {
struct OperationRuntime;
class TransferEngine {
public:
    explicit TransferEngine(OperationRuntime& runtime):runtime_(runtime){}
    Json snapshot(const std::string& id);
    Json publish(Json m,const Json& parents,SnapshotJob* job=nullptr);
    std::map<std::string,Json> frontier();
private:
    OperationRuntime& runtime_;
    Json publish_original(Json,const Json&,SnapshotJob*);
    void begin_progress(const std::string&,uint64_t,uint64_t,uint64_t) noexcept;
    void advance_progress(uint64_t,uint64_t,uint64_t,uint64_t transferred=0) noexcept;
    void phase_progress(const std::string&) noexcept;
    void checkpoint();
    std::string object(const Bytes&);
    Bytes fetch(const std::string&);
    fs::path cache(const std::string&);
};
}
