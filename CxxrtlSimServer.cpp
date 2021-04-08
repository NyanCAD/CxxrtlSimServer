#include "api/Simulator.capnp.h"
#include <kj/debug.h>
#include <kj/filesystem.h>
#include <kj/exception.h>
#include <kj/async-io.h>
#include <capnp/ez-rpc.h>
#include <capnp/message.h>
#include <iostream>
#include <vector>
#include <map>
#include <complex>
#include <sstream>
#include <functional>
#include <dlfcn.h>


class RunImpl;

class ResultImpl final : public Sim::Result::Server
{
public:
    ResultImpl(RunImpl* cmd) : cmd(cmd) {}

    kj::Promise<void> read(ReadContext context);

    RunImpl *cmd;    
};

struct bit {
    uint32_t* ptr;
    size_t offset;
};

typedef int ( *rtlmain )(int argc, const char* argv[]);

typedef std::function<void(double ts, std::map<std::string, struct bit> bits)> sample_t;


class RunImpl final : public Sim::Run::Server
{
public:
    RunImpl(std::string libpath) : libpath(libpath) { }

    kj::Promise<void> run(RunContext context)
    {
        void* m_dll = dlopen(("./"+libpath).c_str(), RTLD_LAZY);
        if(m_dll == nullptr) {
            throw KJ_EXCEPTION(FAILED, dlerror());
        }
        auto mainfn = (rtlmain) dlsym(m_dll, "main" );
        KJ_ASSERT_NONNULL(mainfn);
        auto sample = (sample_t*) dlsym(m_dll, "cxxrtl_stream_sample" );
        *sample = [this](double ts, std::map<std::string, struct bit> bits) {
            auto data = this->data.lockExclusive();
            auto time = this->time.lockExclusive();
            time->push_back(ts);
            for (auto &it : bits) {
                bool bit = (*it.second.ptr >> it.second.offset) & 1;
                (*data)[it.first].push_back(bit);
                // std::cout << "server " << it.first << ": " << bit << std::endl;
            }
        };

        *is_running.lockExclusive() = true;
        thread = kj::heap<kj::Thread>([m_dll, mainfn, this]() {
            mainfn(0, nullptr);
            *this->is_running.lockExclusive() = false;
            if(dlclose(m_dll)) {
                std::cout << dlerror() << std::endl;
            }
        });
        auto res = kj::heap<ResultImpl>(this);
        context.getResults().setResult(kj::mv(res));
        return kj::READY_NOW;
    }


    std::string libpath;
    kj::Own<kj::Thread> thread;

    kj::MutexGuarded<std::map<std::string, std::vector<bool>>>data;
    kj::MutexGuarded<std::vector<double>>time;
    kj::MutexGuarded<bool> is_running;
};

kj::Promise<void> ResultImpl::read(ReadContext context)
{
    auto data = cmd->data.lockExclusive();
    auto time = cmd->time.lockExclusive();

    auto res = context.getResults();
    res.setScale("time");
    res.setMore(*cmd->is_running.lockExclusive());
    auto datlist = res.initData(data->size()+1);
    int i = 0;
    datlist[i].setName("time");
    auto dat = datlist[i].getData();
    i++;
    auto list = dat.initReal(time->size());
    for (size_t j = 0; j < time->size(); j++)
    {
        list.set(j, (*time)[j]);
    }
    time->clear();

    for (auto &it : *data)
    {
        datlist[i].setName(it.first);
        auto dat = datlist[i].getData();
        i++;
        auto list = dat.initDigital(it.second.size());
        for (size_t j = 0; j < it.second.size(); j++)
        {
            list.set(j, it.second[j]);
        }
        it.second.clear();
    }
    return kj::READY_NOW;
}

class SimulatorImpl final : public Sim::Cxxrtl::Server
{
public:
    SimulatorImpl(const kj::Directory &dir) : dir(dir) {}

    kj::Promise<void> loadFiles(LoadFilesContext context) override
    {
        auto files = context.getParams().getFiles();
        for (Sim::File::Reader f : files)
        {
            kj::Path path = kj::Path::parse(f.getName());
            auto file = dir.replaceFile(path, kj::WriteMode::CREATE | kj::WriteMode::MODIFY | kj::WriteMode::CREATE_PARENT);
            file->get().write(0, f.getContents());
            file->commit();
        }

        // build shared library
        for (Sim::File::Reader f : files)
        {
            std::cout << f.getName().cStr() << std::endl;
            if(f.getName() == "Makefile") {
                system("make");
                break;
            } else if(f.getName() == "CMakeLists.txt") {
                system("cmake . && cmake --build .");
                break;
            }
        }

        std::string name;
        for (auto &f : dir.listNames())
        {
            if(f.endsWith(".so")) {
                name = f.cStr();
                break;
            }
        }
        auto res = context.getResults();
        auto commands = kj::heap<RunImpl>(name);
        res.setCommands(kj::mv(commands));
        return kj::READY_NOW;
    }

    const kj::Directory &dir;
};

int main(int argc, const char *argv[])
{
    kj::Own<kj::Filesystem> fs = kj::newDiskFilesystem();
    const kj::Directory &dir = fs->getCurrent();

    // Set up a server.
    std::string listen = "*:5923";
    if (argc == 2) {
        listen = argv[1];
    }
    capnp::EzRpcServer server(kj::heap<SimulatorImpl>(dir), listen);

    auto &waitScope = server.getWaitScope();
    server.getIoProvider().getTimer();
    uint port = server.getPort().wait(waitScope);
    std::cout << "Listening on port " << port << "..." << std::endl;

    // Run forever, accepting connections and handling requests.
    kj::NEVER_DONE.wait(waitScope);
}
