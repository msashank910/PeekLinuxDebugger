#pragma once

#include <string>
#include <sys/types.h>
#include <cstdint>
#include <vector>
#include <array>
#include <optional>
#include <functional>


class MemoryMap {

public: 
    MemoryMap();    //default until actually constructed in initializeMemoryMapAndLoadAddress()
    MemoryMap(pid_t pid_, const std::string& pathToExecutable);
    MemoryMap& operator=(const MemoryMap& other);
    void reload();
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
        const std::string name;
        const Path p;
    };
    
    static inline constexpr int pathCount = 10;
    static const std::array<PathDescriptor, 10> pathDescriptorList;
    static std::string getNameFromPath(Path p);

    struct Permissions{
        bool read;
        bool write;
        bool execute;
        bool shared;
    };

    struct MemoryChunk {
        uint64_t addrLow;
        uint64_t addrHigh;
        Permissions perms;
        Path path;
        std::string pathname;
       // const std::string pathSuffix;     //can be empty if path type has no suffix
        //suffix/tid may be needed later 
        bool isExec() const;
    };


    static bool canRead(MemoryChunk c);
    static bool canWrite(MemoryChunk c);
    static bool canExecute(MemoryChunk c);
    static bool isShared(MemoryChunk c);
    static std::string getFileNameFromChunk(MemoryChunk c);
    
    const std::vector<MemoryChunk>& getChunks() const;
    void printChunk(uint64_t pc) const;
    void dumpChunks() const;
    std::optional<std::reference_wrapper<const MemoryChunk>> getChunkFromAddr(uint64_t addr) const;

    bool initialized() const;

private:
    //may need to incorporate mutex --> look into lock_guard for RAII
    pid_t pid_;
    std::string exec_;
    std::vector<MemoryChunk> chunks_;

    Path getPathFromFullPathname(std::string_view s) const;
};