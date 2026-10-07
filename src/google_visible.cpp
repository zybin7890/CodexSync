#include "google_drive.hpp"
#include <stdexcept>
namespace cxs {
namespace {
Json response_json(const Response& response){if(response.status<200||response.status>=300)throw std::runtime_error("Google visible folder request failed: HTTP "+std::to_string(response.status));return Json::parse(response.body.begin(),response.body.end());}
Bytes encoded(const Json& value){auto text=value.dump();return {text.begin(),text.end()};}
bool valid_id(const std::string& value){return !value.empty()&&value.size()<=200&&std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_';});}
}
std::string GoogleDrive::visible_folder(bool create){
    if(folder_checked_)return folder_id_;
    const auto account=account_identity();const auto repository=prefix_.substr(14,prefix_.size()-15);
    const auto binding=origin_+"\n"+prefix_+"\n"+account;
    const auto id=digest(Bytes(binding.begin(),binding.end()),folder_key_);
    const auto file=state_/"google-folders"/(id+".cxs");
    if(folder_id_.empty()&&fs::exists(file)){auto value=Json::parse(open(read_bytes(file,8192),folder_key_,"google-folder:"+id));if(value.at("account")!=account||value.at("repository")!=repository)throw std::runtime_error("Google folder binding mismatch");folder_id_=value.at("folder_id");}
    if(folder_id_.empty()){
        auto find_folder=[&](const std::string& expression){auto url=origin_+"/drive/v3/files?spaces=drive&pageSize=1000&fields="+url_encode("nextPageToken,incompleteSearch,files(id)")+"&q="+url_encode("trashed = false and mimeType = 'application/vnd.google-apps.folder' and "+expression);auto result=response_json(call(url,"GET"));if(result.value("incompleteSearch",false)||result.contains("nextPageToken"))throw std::runtime_error("incomplete Google folder search");std::string found;for(const auto& item:result.at("files")){auto next=item.at("id").get<std::string>();if(!valid_id(next))throw std::runtime_error("invalid Google folder ID");if(found.empty()||next<found)found=next;}return found;};
        auto create_folder=[&](const std::string& name,const std::string& parent,const Json& properties){Json metadata={{"name",name},{"mimeType","application/vnd.google-apps.folder"},{"appProperties",properties}};if(!parent.empty())metadata["parents"]=Json::array({parent});auto result=response_json(call(origin_+"/drive/v3/files?fields=id","POST",encoded(metadata),{"Content-Type: application/json; charset=UTF-8"}));auto created=result.at("id").get<std::string>();if(!valid_id(created))throw std::runtime_error("invalid created Google folder ID");return created;};
        // appProperties identifies the application; a display name is never
        // used as a repository identity. The selected folder ID is durable.
        folder_id_=find_folder("appProperties has { key='cxs_repository' and value='"+repository+"' }");
        if(folder_id_.empty()&&create){auto root=find_folder("appProperties has { key='cxs_app' and value='codex-sync-v1' }");if(root.empty())root=create_folder("CodexSync","",{{"cxs_app","codex-sync-v1"}});folder_id_=create_folder(repository,root,{{"cxs_repository",repository},{"cxs_app","codex-sync-v1"}});}
    }
    if(folder_id_.empty())return {};
    auto metadata=response_json(call(origin_+"/drive/v3/files/"+folder_id_+"?fields="+url_encode("id,mimeType,trashed,spaces,appProperties"),"GET"));
    if(metadata.at("id")!=folder_id_||metadata.at("mimeType")!="application/vnd.google-apps.folder"||metadata.value("trashed",false)||metadata.at("spaces")!=Json::array({"drive"})||metadata.value("appProperties",Json::object()).value("cxs_repository",std::string())!=repository)throw std::runtime_error("Google repository folder identity or storage space mismatch");
    if(create)write_atomic(file,seal(encoded(Json{{"folder_id",folder_id_},{"account",account},{"repository",repository}}),folder_key_,"google-folder:"+id));folder_checked_=true;return folder_id_;
}
Json GoogleDrive::location(){auto folder=storage_mode_=="visible"?(original_?original_folder("",false):visible_folder(false)):std::string();return {{"storage_mode",storage_mode_},{"repository",prefix_.substr(14,prefix_.size()-15)},{"folder_id",folder},{"folder_url",folder.empty()?"":"https://drive.google.com/drive/folders/"+folder}};}
}
