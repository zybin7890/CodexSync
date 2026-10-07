#pragma once
#include "core.hpp"
namespace cxs {
struct JobInterrupted:std::runtime_error {std::string phase;explicit JobInterrupted(std::string value):std::runtime_error("task "+value+"; prepared snapshot retained"),phase(std::move(value)){};};
class JobCoordinator {
public:
    explicit JobCoordinator(const fs::path& state);
    ~JobCoordinator();
    JobCoordinator(const JobCoordinator&)=delete;
    JobCoordinator& operator=(const JobCoordinator&)=delete;
    static bool busy(const fs::path& state);
    static Json control(const fs::path& state,const std::string& task,const std::string& action);
    static void checkpoint(const fs::path& state,const std::string& job,const std::string& operation);
private:
    struct Native;
    std::unique_ptr<Native> native_;
};
}
