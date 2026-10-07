#pragma once
#include "core.hpp"
namespace cxs {
bool encrypt_entry(const Json& config,const Json& entry);
void protect_file(const fs::path& input,const fs::path& output,const Key&);
void unprotect_file(const fs::path& input,const fs::path& output,const Key&);
Bytes protect_key(const Key&,const std::string& password);
Key unprotect_key(const Bytes&,const std::string& password);
}
