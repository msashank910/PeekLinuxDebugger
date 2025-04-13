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
SymbolMap::SymbolMap(const elf::elf& elf, uint64_t loadAddress) : elf_(elf), loadAddress_(loadAddress) {}

SymbolMap::SymbolMap(SymbolMap&&) = default;
SymbolMap& SymbolMap::operator=(SymbolMap&&) = default;
SymbolMap::~SymbolMap() = default;

SymbolMap::SymbolMap(const SymbolMap&) = delete;
SymbolMap& SymbolMap::operator=(const SymbolMap&) = delete;


std::string SymbolMap::getNameFromSym(SymbolMap::Sym s) {
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

std::vector<SymbolMap::Symbol> SymbolMap::getSymbolListFromName(const std::string& name, bool strict) {
    auto it = nonStrictSymbolCache_.find(name);
    if(it != nonStrictSymbolCache_.end() && !strict) return it->second;

    std::vector<Symbol> strictMatches;
    if(it != nonStrictSymbolCache_.end()) {
        for(auto& symbol : it->second) {
            if(name == symbol.name) strictMatches.push_back(symbol);
        }
        return strictMatches;
    }

    std::vector<Symbol> nonStrictMatches;
    for(auto& section : elf_.sections()) {
        auto type = section.get_hdr().type;
        if(type != elf::sht::symtab && type != elf::sht::dynsym) continue;

        // std::cout << "\n------------------------TEST-BEGIN--------------------------\n";
        for(auto symbol : section.as_symtab()) {
            auto symName = symbol.get_name();
            if(symName.empty()) continue;
            auto demangled = demangleSymbol(symName);
            if(demangled) symName = demangled.value();

            if(strict && symName == name) {
                auto& symData = symbol.get_data();
                auto symElfType = getSymFromElf(symData.type());
                nonStrictMatches.emplace_back(Symbol(symElfType, symName, 
                    std::bit_cast<uintptr_t>(symData.value)));
                strictMatches.emplace_back(Symbol(symElfType, symName, 
                    std::bit_cast<uintptr_t>(symData.value)));    
            }
            else if(symName.find(name) != std::string::npos) {
                auto& symData = symbol.get_data();
                auto symElfType = getSymFromElf(symData.type());
                nonStrictMatches.emplace_back(Symbol(symElfType, symName, 
                    std::bit_cast<uintptr_t>(symData.value)));
            }
        }
    }
    //Only cache non-strict symbols
    nonStrictSymbolCache_[name] = std::move(nonStrictMatches);
    if(strict) return strictMatches;
    it = nonStrictSymbolCache_.find(name);
    return it->second;
}

void SymbolMap::dumpSymbolList(const std::vector<Symbol>& symbolList, const std::string& name, bool strict) {
    if(symbolList.empty()) {
        std::cout << "[error] Symbol list for '" << name << "' is empty!\n";
        return;
    }
    int symCount = 1;
    for(const auto&[sym, symName, addr] : symbolList) {
        if(strict && symName != name) continue;
        std::cout << "\t" << std::dec << symCount++ << ") " << symName
            << " (" << getNameFromSym(sym) << ")"
            << " --> 0x" << std::hex << std::uppercase << addr << "\n";
    }
}

void SymbolMap::dumpSymbolCache(bool strict) const {
    if(nonStrictSymbolCache_.empty()) {
        std::cout << "[error] No symbol caches exist!";
        return;
    }
    std::cout << "\n--------------------------------------------------------\n";
    int count = 1;
    for(const auto&[name, cache] : nonStrictSymbolCache_) {
        std::cout << "(" << std::dec << count++ << ") \"" << name << "\"\n";
        dumpSymbolList(cache, name, strict);
        std::cout << "--------------------------------------------------------\n";
    }
}

void SymbolMap::dumpSymbolCache(const std::string& name, bool strict) const {
    auto it = nonStrictSymbolCache_.find(name);
    if(it == nonStrictSymbolCache_.end()) {
        std::cout << "[error] No symbol cache for '" << name << "'!";
        return;
    }
    std::cout << "\n--------------------------------------------------------\n";
    std::cout << "[debug] Dumping symbols with name '" << name << "':\n";
    dumpSymbolList(it->second, name, strict);
    std::cout << "--------------------------------------------------------\n";
}



