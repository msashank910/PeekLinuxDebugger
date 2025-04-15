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
    MemoryMap() = default;    //default until actually constructed in initializeMemoryMapAndLoadAddress()
    MemoryMap(pid_t pid_, const std::string& pathToExecutable);

    MemoryMap(MemoryMap&&) = default;
    MemoryMap& operator=(MemoryMap&&) = default;
    ~MemoryMap() = default;

    MemoryMap(const MemoryMap&) = delete;
    MemoryMap& operator=(const MemoryMap&) = delete;
    
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

    struct Chunk {
        uint64_t addrLow;   //inclusive
        uint64_t addrHigh;  //non-inclusive
        Permissions perms;
        Path path;
        std::string pathname;
       // const std::string pathSuffix;     //can be empty if path type has no suffix
        //suffix/tid may be needed later 
        bool isPathtypeExec() const; 
        bool contains(uint64_t addr) const;     //return if addr in range [addrLow, addrHigh)

        bool canRead() const;
        bool canWrite() const;
        bool canExecute() const;
    };


    // static bool canRead(Chunk& c);
    // static bool canWrite(Chunk& c);
    // static bool canExecute(Chunk& c);
    // static bool isShared(Chunk& c);
    static std::string getFileNameFromChunk(Chunk c);
    
    const std::vector<Chunk>& getChunks() const;
    void printChunk(uint64_t pc) const;
    void dumpChunks() const;

    std::optional<std::reference_wrapper<const Chunk>> getChunkFromAddr(uint64_t addr) const;
    bool canRead(uint64_t addr);
    bool canWrite(uint64_t addr);

    bool initialized() const;

private:
    //may need to incorporate mutex --> look into lock_guard for RAII
    pid_t pid_ = 0;
    std::string exec_;
    std::vector<Chunk> chunks_;

    Path getPathFromFullPathname(std::string_view s) const;
};