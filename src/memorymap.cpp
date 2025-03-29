#include "../include/memorymap.h"
#include "../include/util.h"

#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>
#include <bit>
#include <fstream>
#include <optional>
#include <functional>



using util::validHexStol;


const std::array<MemoryMap::PathDescriptor, 10> MemoryMap::pathDescriptorList {{
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


bool MemoryMap::MemoryChunk::isExec() const { return path == Path::exec; }

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
        auto addrHigh = view.substr(dashPos + 1, spacePos - (dashPos + 1));
        if(!validHexStol(low, addrLow) || !validHexStol(high, addrHigh)) {
            file.close();
            throw std::runtime_error("Address space not resolved correctly\n");
        }

        view = view.substr(spacePos + 1);
        if(view[0] == 'r') read = true;
        if(view[1] == 'w') write = true;
        if(view[2] == 'x') execute = true;
        if(view[3] == 's') shared = true;

        spacePos = view.find_last_of(" \t");
        view = view.substr(spacePos + 1);

        pathname = std::string(view);
        path = getPathFromFullPathname(view);
        Permissions perms(read, write, execute, shared);
        chunks_.emplace_back(MemoryChunk(low, high, perms, path, pathname)); //fix suffix logic
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


 MemoryMap::Path MemoryMap::getPathFromFullPathname(std::string_view pathname) const {
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
        pathname = std::string(pathname.substr(1, pathname.find_first_of(']') - 1));
        //std::cout << "DEBUG: " << pathname << "\n";
        auto suffix = pathname.find(':');
        if(suffix != std::string_view::npos) pathname = pathname.substr(0, suffix);
    }

    auto pathDescriptor = std::find_if(pathDescriptorList.begin(), pathDescriptorList.end(),
        [pathname] (auto&& pd) {
            return pd.name == pathname;
        }
    );    

    //returns path enum
    if(pathDescriptor == pathDescriptorList.end()) {
        return Path::anon;
    }
    return pathDescriptor->p;
}

void MemoryMap::reload() {
    MemoryMap newMM(pid_, exec_);
    
    chunks_ = newMM.chunks_;
}

std::string MemoryMap::getNameFromPath(Path p) {
    //update, then print function, then debug memory map, then work on step functions
    auto res = std::find_if(pathDescriptorList.begin(), pathDescriptorList.end(), 
        [=] (auto&& pd) {
            return pd.p == p;
        }
    );

    if(res == pathDescriptorList.end()) {
        return "anon";
    }
    
    return res->name;
}

void MemoryMap::printChunks() {
    int i = 1;
    std::cout << "\n";
    for(const auto& c : chunks_) {
        const auto& perms = c.perms;
        std::cout << std::dec << i << ") " << c.pathname << "\n" 
            << "Path Type: " << getNameFromPath(c.path) << "\n"
            << "Permissions (read-write-execute-shared): " 
                << (perms.read ? "r" : "-")
                << (perms.write ? "w" : "-")
                << (perms.execute ? "x" : "-")
                << (perms.shared ? "s" : "p")
                << "\n"
            << "Lowest Address: " << std::hex << std::uppercase << c.addrLow << "\n"
            << "Highest Address: " << c.addrHigh << "\n"
            << "(" << c.addrLow << "-" << c.addrHigh << ")\n"
            << "--------------------------------------------------------\n";
        ++i;
    }
}

std::optional<std::reference_wrapper<const MemoryMap::MemoryChunk>>MemoryMap::getChunkFromAddr(uint64_t addr) const {
    for(const auto& c : chunks_) {
        auto low = c.addrLow;
        auto high = c.addrHigh;
        
        if(addr >= low && addr <= high ) {
            return std::reference_wrapper<const MemoryChunk>(c);
        }
    }
    return std::nullopt;
}

bool MemoryMap::initialized() const {
    return (pid_ && !exec_.empty() && !chunks_.empty());
}