#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include <elf/elf++.hh>


class SymbolMap {

public:
    SymbolMap();
    SymbolMap(const elf::elf& elf);
    
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

    std::string getNameFromSym(Sym s) const;
    Sym getSymFromElf(elf::stt s) const;

    const std::vector<Symbol>& getSymbolListFromName(const std::string& name);
    void dumpSymbolCache() const;
    void dumpSymbolCache(const std::string& name) const;
    
private:
    elf::elf elf_;
    std::unordered_map<std::string, std::vector<Symbol>> symbolCache;

};