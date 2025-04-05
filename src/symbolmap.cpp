#include "../include/symbolmap.h"
#include "../include/util.h"

#include <elf/elf++.hh> 

#include <bit>
#include <string>
#include <unordered_map>
#include <iostream>
#include <vector>
#include <cstdint>

using util::demangleSymbol;

SymbolMap::SymbolMap() = default;
SymbolMap::SymbolMap(const elf::elf& elf) : elf_(elf) {}

SymbolMap::SymbolMap(SymbolMap&&) = default;
SymbolMap& SymbolMap::operator=(SymbolMap&&) = default;
SymbolMap::~SymbolMap() = default;

SymbolMap::SymbolMap(const SymbolMap&) = delete;
SymbolMap& SymbolMap::operator=(const SymbolMap&) = delete;


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

        std::cout << "\n------------------------TEST-BEGIN--------------------------\n";
        for(auto symbol : section.as_symtab()) {
            auto symName = symbol.get_name();
            auto demangled = demangleSymbol(symName);
            
            if(demangled) symName = demangled.value();
            if(symName.find(name) != std::string::npos) {
                std::cout /*<< "Symbol mangled: " << symbol.get_name()*/
                    << "[unmangled] --> " << symName << "\n";

            }

            if(symName == name) {
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

void SymbolMap::dumpSymbolCache() const {
    if(symbolCache.empty()) {
        std::cout << "[error] No symbol caches exist!";
        return;
    }
    std::cout << "\n--------------------------------------------------------\n";
    int count = 1;
    for(const auto&[name, cache] : symbolCache) {
        std::cout << "(" << std::dec << count++ << ") " << name << "\n";
        int symCount = 1;
        for(const auto&[sym, type, addr] : cache) {
            std::cout << "\t" << std::dec << symCount++ << ") " << type
                << " --> " << std::hex << std::uppercase << addr << "\n";
        }
    std::cout << "--------------------------------------------------------\n";
    }
}

void SymbolMap::dumpSymbolCache(const std::string& name) const {
    auto it = symbolCache.find(name);
    if(it == symbolCache.end()) {
        std::cout << "[error] No symbol cache for " << name << "!";
        return;
    }
    std::cout << "\n--------------------------------------------------------\n";
    std::cout << "[debug] Symbols with name " << name << ":\n";
    int symCount = 1;
    for(const auto&[sym, type, addr] : it->second) {
        std::cout << "\t" << std::dec << symCount++ << ") " << type
            << " --> " << std::hex << std::uppercase << addr << "\n";
    }
    std::cout << "--------------------------------------------------------\n";
}



