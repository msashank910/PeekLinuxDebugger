#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include <elf/elf++.hh>


class SymbolMap {

public:
    SymbolMap();
    SymbolMap(const elf::elf& elf, const uint64_t loadAddress);
    
    SymbolMap(SymbolMap&&);
    SymbolMap& operator=(SymbolMap&&);
    ~SymbolMap();

    SymbolMap(const SymbolMap&);
    SymbolMap& operator=(const SymbolMap&);



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
        bool strict = true);
    void dumpSymbolCache(bool strict = true) const;
    void dumpSymbolCache(const std::string& name, bool strict = true) const;
    
private:
    elf::elf elf_;
    uint64_t loadAddress_;
    std::unordered_map<std::string, std::vector<Symbol>> nonStrictSymbolCache_;

};