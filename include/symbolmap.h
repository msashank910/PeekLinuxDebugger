#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include <elf/elf++.hh>

#include "./config.h"


class SymbolMap {

public:
    SymbolMap() = default;
    SymbolMap(const elf::elf& elf, const uint64_t loadAddress, Config::SymbolConfig* config);
    
    SymbolMap(SymbolMap&&) = default;
    SymbolMap& operator=(SymbolMap&&) = default;
    ~SymbolMap() = default;

    SymbolMap(const SymbolMap&) = delete;
    SymbolMap& operator=(const SymbolMap&) = delete;

    bool initialized();

    uint8_t getMinCachedStringLength();
    void setMinCachedStringLength(uint8_t length);
    size_t getMaxSymbolCacheSize();
    void setMaxSymbolCacheSize(size_t size);
    void clearCache();



    enum class Sym {
        notype,
        object,
        func,
        section,
        file,
        NULL_SYM
    };
    
    struct Symbol {
        Sym s;
        std::string name;
        uintptr_t addr;
    };

    static std::string getNameFromSym(Sym s);
    Sym getSymFromElf(elf::stt s) const;

    std::vector<Symbol> getSymbolListFromName(const std::string& name, bool strict = true);
    static void dumpSymbolList(const std::vector<Symbol>& symbolList, const std::string& name, 
        bool strict = false);
    void dumpSymbolCache(bool strict = false) const;
    void dumpSymbolCache(const std::string& name, bool strict = false) const;
    
private:
    elf::elf elf_;
    uint64_t loadAddress_ = 0;
    std::unordered_map<std::string, std::vector<Symbol>> nonStrictSymbolCache_;
    Config::SymbolConfig* config_ = nullptr;
    void configure();


};