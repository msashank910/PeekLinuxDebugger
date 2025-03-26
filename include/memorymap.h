#pragma once

#include <string>
#include <sys/types.h>
#include <cstdint>
#include <vector>
#include <array>

class MemoryMap {

public: 
    MemoryMap();    //default until actually constructed in initializeMemoryMapAndLoadAddress()
    MemoryMap(pid_t pid_, const std::string& pathToExecutable);
    MemoryMap& operator=(const MemoryMap& other);
    //reload()
    //parseProcPidMaps()
    

    enum class Path {
        stack,
        stackTID,
        heap,
        vdso,
        vvar,
        vsyscall,
        exec,   //executable
        so,     //shared library
        mmap,   //memory mapped
        anon
    };

    struct PathDescriptor {
        const std::string type;
        const Path p;
    };
    
    static inline constexpr int pathCount = 10;
    static const std::array<PathDescriptor, 10> pathDescriptorList;

    struct Permissions{
        const bool read;
        const bool write;
        const bool execute;
        const bool shared;
    };

    struct MemoryChunk {
        const uint64_t addrLow;
        const uint64_t addrHigh;
        const Permissions perms;
        const Path path;
        const std::string pathname;
        const std::string pathSuffix;     //can be empty if path type has no suffix
        
    };

    Path getPathFromStringPathname(std::string_view s) const;

    const std::vector<MemoryChunk>& getChunks() const;
    bool canRead(MemoryChunk) const;
    bool canWrite(MemoryChunk) const;
    bool canExecute(MemoryChunk) const;
    bool isShared(MemoryChunk) const;


private:
    //may need to incorporate mutex --> look into lock_guard for RAII
    pid_t pid_;
    std::string exec_;
    std::vector<MemoryChunk> chunks_;


};