#include "../include/symbolmap.h"
#include "../include/util.h"
#include "../include/config.h"

#include <elf/elf++.hh> 

#include <bit>
#include <string>
#include <unordered_map>
#include <iostream>
#include <vector>
#include <cstdint>

using util::demangleSymbol;

// SymbolMap::SymbolMap() = default;
SymbolMap::SymbolMap(const elf::elf& elf, uint64_t loadAddress, Config::SymbolConfig* config) : 
    elf_(elf), loadAddress_(loadAddress), config_(config) { }

// SymbolMap::SymbolMap(SymbolMap&&) = default;
// SymbolMap& SymbolMap::operator=(SymbolMap&&) = default;
// SymbolMap::~SymbolMap() = default;

// SymbolMap::SymbolMap(const SymbolMap&) = delete;
// SymbolMap& SymbolMap::operator=(const SymbolMap&) = delete;

bool SymbolMap::initialized() {
    return elf_.valid() && config_;
}

void SymbolMap::configure() {
    auto deletion = config_->configureCache();
    for(const auto& key : deletion) {
        auto found = nonStrictSymbolCache_.find(key);
        if(found == nonStrictSymbolCache_.end()) continue;
        
        nonStrictSymbolCache_.erase(found);
    }
}


uint8_t SymbolMap::getMinCachedStringLength() { return config_->minCachedStringLength_; }
void SymbolMap::setMinCachedStringLength(uint8_t length) {
    config_->minCachedStringLength_ = length;
    return configure();
}

size_t SymbolMap::getMaxSymbolCacheSize() { return config_->symbolCacheSize_; }
void SymbolMap::setMaxSymbolCacheSize(size_t size) {
    config_->symbolCacheSize_ = size;
    return configure();
}

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
    if(name.length() >= static_cast<size_t>(config_->minCachedStringLength_)) {
        nonStrictSymbolCache_[name] = std::move(nonStrictMatches);
        auto erase = config_->touchKey(name);
        if(!erase.empty()) {
            auto found = nonStrictSymbolCache_.find(erase);
            if(found != nonStrictSymbolCache_.end()) {
                nonStrictSymbolCache_.erase(found);
            }
        }
        if(strict) return strictMatches;
        return nonStrictSymbolCache_[name];
        // it = nonStrictSymbolCache_.find(name);
        // return it->second;
    }
    //If the name is too short to be cached
    return (strict ? strictMatches : nonStrictMatches);
    
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
            " --> 0x" << std::hex << std::uppercase << addr << "\n";
    }
}

void SymbolMap::dumpSymbolCache(bool strict) const {
    if(nonStrictSymbolCache_.empty()) {
        std::cout << "\n[error] No symbol caches exist!";
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
        std::cout << "\n[error] No symbol cache for '" << name << "'!";
        return;
    }
    std::cout << "\n--------------------------------------------------------\n"
        "[debug] Dumping symbols with name '" << name << "':\n";
    dumpSymbolList(it->second, name, strict);
    std::cout << "--------------------------------------------------------\n";
}



