#include "operation_runtime.hpp"
#include "capture_engine.hpp"
#include "transfer_engine.hpp"
#include "restore_engine.hpp"
namespace cxs {
Json OperationRuntime::entry(const fs::path& p,const std::string& root,const std::string& rel,bool scanning,bool shadow){return CaptureEngine(*this).entry(p,root,rel,scanning,shadow);}
Json OperationRuntime::scan(){return CaptureEngine(*this).scan();}
Json OperationRuntime::incremental_capture(ChangeCatalog& catalog,Json& next_catalog){return CaptureEngine(*this).incremental_capture(catalog,next_catalog);}
Json OperationRuntime::snapshot(const std::string& id){return TransferEngine(*this).snapshot(id);}
Json OperationRuntime::publish(Json m,const Json& parents,SnapshotJob* job){return TransferEngine(*this).publish(m,parents,job);}
std::map<std::string,Json> OperationRuntime::frontier(){return TransferEngine(*this).frontier();}
fs::path OperationRuntime::root_path(const std::string& id){return RestoreEngine(*this).root_path(id);}
void OperationRuntime::materialize(const Json& e,const fs::path& dest){RestoreEngine(*this).materialize(e,dest);}
void OperationRuntime::restore_progress(const Json& entries){RestoreEngine(*this).restore_progress(entries);}
}
