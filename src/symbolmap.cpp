#include "../include/symbolmap.h"

#include <elf/elf++.hh> 

#include <bit>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>


SymbolMap::SymbolMap() : elf_() {}
SymbolMap::SymbolMap(const elf::elf& elf) : elf_(elf) {}

SymbolMap& SymbolMap::operator=(SymbolMap&& other) {
    if(this != &other) {
        
    }
}

std::string SymbolMap::getNameFromSym(SymbolMap::Sym s) const {
    switch(s) {
        case Sym::notype:
            return "notype";
        case Sym::object:
            return "object";
        case Sym::func:
            return "func";
        case Sym::section:
            return "section";
        case Sym::file:
            return "file";
        default:
            break;
    }
    return "NULL_SYM";
}

SymbolMap::Sym SymbolMap::getSymFromElf(elf::stt s) const {
    switch(s) {
        case elf::stt::notype:
            return Sym::notype;
        case elf::stt::object:
            return Sym::object;
        case elf::stt::func:
            return Sym::func;
        case elf::stt::section:
            return Sym::section;
        case elf::stt::file:
            return Sym::file;
        default:
            break;
    }
    return Sym::NULL_SYM;
}

const std::vector<SymbolMap::Symbol>& SymbolMap::getSymbolListFromName(const std::string& name) {
    auto it = symbolCache.find(name);
    if(it != symbolCache.end()) return it->second;

    std::vector<Symbol> v;

    for(auto& section : elf_.sections()) {
        auto type = section.get_hdr().type;
        if(type != elf::sht::symtab && type != elf::sht::dynsym) continue;

        for(auto symbol : section.as_symtab()) {
            if(symbol.get_name() == name) {
                auto& symData = symbol.get_data();
                auto symElfType = getSymFromElf(symData.type());

                v.emplace_back(Symbol(symElfType, getNameFromSym(symElfType), 
                    std::bit_cast<uintptr_t>(symData.value)));
            }
        }
    }

    symbolCache[name] = std::move(v);
    it = symbolCache.find(name);
    return it->second;
    
}


