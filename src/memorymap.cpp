#include "../include/memorymap.h"
#include "../include/util.h"

#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>
#include <bit>
#include <fstream>


using util::validHexStol;


const std::array<MemoryMap::PathDescriptor, 10> pathDescriptorList {{
    {"stack", MemoryMap::Path::stack},
    {"stackTID", MemoryMap::Path::stackTID},
    {"heap", MemoryMap::Path::heap},
    {"vdso", MemoryMap::Path::vdso},
    {"vvar", MemoryMap::Path::vvar},
    {"vsyscall", MemoryMap::Path::vsyscall},
    {"exec", MemoryMap::Path::exec},    //executable
    {"so", MemoryMap::Path::so},     //shared library
    {"mmap", MemoryMap::Path::mmap},   //memory mapped
    {"anon", MemoryMap::Path::anon}
}};



MemoryMap::MemoryMap() : pid_(0), exec_("") {}

MemoryMap::MemoryMap(pid_t pid, const std::string& pathToExectuable) : pid_ (pid), exec_(pathToExectuable) {
    std::ifstream file;
    file.open("/proc/" + std::to_string(pid) + "/maps");
    if(!file.is_open()) {
        throw std::runtime_error("/proc/pid/maps could not be opened! Check permisions.\n");
    }
    std::string line = "";
    
    while(std::getline(file, line, '\n')) {
        uint64_t low, high;
        bool read = false, write = false, execute = false, shared = false;
        Path path;
        std::string pathname = "";
        
        std::string_view view(line);
        auto dashPos = view.find_first_of('-');
        auto spacePos = view.find_first_of(' ');

        auto addrLow = view.substr(0, dashPos);
        auto addrHigh = view.substr(dashPos + 1, spacePos);
        if(!validHexStol(low, addrLow) || !validHexStol(high, addrHigh)) {
            file.close();
            throw std::runtime_error("Address space not resolved correctly\n");
        }

        view = view.substr(spacePos + 1);
        spacePos = view.find_first_of(' ');
        auto permissions = view.substr(spacePos);
        if(permissions.find('r') != std::string_view::npos) read = true;
        if(permissions.find('w') != std::string_view::npos) write = true;
        if(permissions.find('x') != std::string_view::npos) execute = true;
        if(permissions.find('s') != std::string_view::npos) shared = true;

        spacePos = view.find_first_of('\t');
        view = view.substr(spacePos + 1);
        pathname = std::string(view);
        path = getPathFromStringPathname(view);
        Permissions perms(read, write, execute, shared);

        chunks_.emplace_back(MemoryChunk(low, high, perms, path, pathname, "")); //fix suffix logic
    }

}

MemoryMap& MemoryMap::operator=(const MemoryMap& other) {
    if(this != &other) {
        this->pid_ = other.pid_;
        this->exec_ = other.exec_;
        this->chunks_ = other.chunks_;
    }
    return *this;
}


const std::vector<MemoryMap::MemoryChunk>& MemoryMap::getChunks() const {
    return chunks_;
}


MemoryMap::Path MemoryMap::getPathFromStringPathname(std::string_view pathname) const {
    if(pathname[0] == '/') {
        auto sharedLibraryCheck = pathname.find(".so");
        if(sharedLibraryCheck != std::string_view::npos) {
            auto possibleSO = pathname.substr(sharedLibraryCheck);
            if(possibleSO.length() == 3 || possibleSO[3] == '.') return Path::so;
        }
        if(pathname == exec_) return Path::exec;
        else return Path::mmap;
    }
    else if(pathname[0] == '[') {
        pathname = std::string(pathname.substr(1, pathname.find_first_of(']')));
    }

    auto pathDescriptor = std::find_if(pathDescriptorList.begin(), pathDescriptorList.end(),
        [pathname] (auto&& pd) {
            if(pd.type == pathname)
                return pd;
        }
    );    

    //returns path enum
    if(pathDescriptor == pathDescriptorList.end()) {
        return Path::anon;
    }
    return pathDescriptor->p;
}