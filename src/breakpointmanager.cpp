#include "../include/debugger.h"
#include "../include/breakpoint.h"
#include "../include/memorymap.h"
#include "../include/util.h"

#include <dwarf/dwarf++.hh>


#include <iostream>
#include <unordered_map>
#include <stdexcept>
#include <utility>
#include <bit>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>


using util::validDecStol;
using util::promptYesOrNo;

/*
    Below are all the Debugger class member functions that involve breakpoints. This is not to be confused 
    with breakpoint.cpp, which deals with the breakpoint class. These member functions handle setting and 
    removing breakpoints in a debugger instance.
*/

std::pair<std::unordered_map<intptr_t, Breakpoint>::iterator, bool>
     Debugger::setBreakpointAtAddress(std::intptr_t address/*, bool skipLineTableCheck*/) {
    
    //First check if it is within an executable memory region or in main process memory space
    auto chunk = memMap_.getChunkFromAddr(address);
    if(!chunk || !((chunk.value().get().canExecute()) || chunk.value().get().isPathtypeExec())) {
        std::cerr << "[error] Invalid Memory Address!";
        return {addrToBp_.end(), false};
    }
    /* I am abandoning this check for now. This is because it is causing way too many problems 
    and likely can be revisited in the future.
    
    //Then check if it is a valid instruction in the line table (only if not special case)
    auto lineEntryItr = getLineEntryFromPC(address);
    if(!skipLineTableCheck && (!lineEntryItr || 
            std::bit_cast<intptr_t>(lineEntryItr.value()->address) != address)) {
        std::cerr << "[error] Address is not an instruction!";
        return {addrToBp_.end(), false};
    } */

    auto [it, inserted] = addrToBp_.emplace(address, Breakpoint(pid_, address));
    if(inserted) {
        if(!it->second.enable()) {  //checks for success of breakpoint::enable()
            std::cerr << "[error] Invalid Memory Address!";
            addrToBp_.erase(it);
            return {addrToBp_.end(), false};
        }
    }
    return {it, inserted};
}



std::pair<std::unordered_map<intptr_t, Breakpoint>::iterator, bool> 
     Debugger::setBreakpointAtFunctionName(const std::string_view name) {
    std::vector<dwarf::die> matching;
    //std::cout << "NAME = |" << name << "| " << std::dec << name.length() << std::endl;
    for(const auto& [cu, offset] : functionDies_) {
        int count = 0;
        for(const auto& die : cu->root()) {
            if(die.get_unit_offset() == offset[count] && die.has(dwarf::DW_AT::name)) {
                if(dwarf::at_name(die) == name)
                    matching.push_back(die);
                ++count;
            }
        }
    }
    auto optionalAddr = handleDuplicateFunctionNames(name, matching);
    if(optionalAddr)
        return setBreakpointAtAddress(optionalAddr.value());
    return {addrToBp_.end(), false};    
}

/* Handle Duplicate function names. Takes in a const ref to a vector of dies. */
std::optional<intptr_t>Debugger::handleDuplicateFunctionNames(const std::string_view name, 
     const std::vector<dwarf::die>& functions) {
       
    if(functions.empty()) return std::nullopt;
    else if(functions.size() == 1) {
        auto low = dwarf::at_low_pc(functions[0]);
        auto lineEntryItr = getLineEntryFromPC(low);
        if(!lineEntryItr) {
            throw std::logic_error("[fatal] In Debugger::setBreakpointAtFunctionName() - "
                "function definition with low pc does not have valid line entry\n");
        }
        auto entry = lineEntryItr.value();
        auto addr = (++entry)->address;
        return std::bit_cast<intptr_t>(addLoadAddress(addr));
    }

    auto funcLocation = [] (dwarf::line_table::iterator& it) {
        std::filesystem::path path(it->file->path);
        std::string location = path.filename().string() + ":" + std::to_string(it->line);
        return location;
    };

    std::cout << "\n[info] Multiple matches found for '" << name << "':\n";
    int count = 0;
    auto symbols = symMap_.getSymbolListFromName(std::string(name), false);
    for(const auto& die : functions) {
        // auto& die = dieRef.get();
        auto low = dwarf::at_low_pc(die);
        auto lineEntryItr = getLineEntryFromPC(low);
        std::string fullName = "";
        for(auto symbol : symbols) {    //resolve the symbol name of the function (with parameters)
            if(symbol.addr == low && symbol.s == SymbolMap::Sym::func) {
                fullName = symbol.name;
                break;
            }
        }
        if(!lineEntryItr) {
            throw std::logic_error("[fatal] In Debugger::setBreakpointAtFunctionName() - "
                "function definition with low pc does not have valid line entry\n");
        }
        auto entry = lineEntryItr.value();
        //if symbol name could not be found, just use die's name
        if(fullName.length() == 0) fullName = dwarf::at_name(die);

        std::cout << std::dec << "\t[" << count++ << "] " << fullName
            << " at " << funcLocation(entry) << "\n";
    }

    std::cout << "\n[info] Select one to set a breakpoint (or abort): ";
    std::string selection;
    std::cin >> selection;
    count = 0;
    uint64_t index;

    if(validDecStol(index, selection) && index < functions.size()) {
        auto& die = functions[index];
        auto lineEntry = getLineEntryFromPC(dwarf::at_low_pc(die));
        auto addr = (++(lineEntry.value()))->address;
        //std::cout << "\n";
        return std::bit_cast<intptr_t>(addLoadAddress(addr));
    }
    return std::nullopt;
}





std::pair<std::unordered_map<intptr_t, Breakpoint>::iterator, bool> 
     Debugger::setBreakpointAtSourceLine(const std::string_view file, const unsigned line) {
    std::vector<std::pair<std::string, intptr_t>> filepathAndAddr;
    std::filesystem::path filePath(file);

    for(auto& cu : dwarf_.compilation_units()) {
        auto& root = cu.root();
        if(root.has(dwarf::DW_AT::name)) {      //check conflicting filepaths
            std::filesystem::path diePath(dwarf::at_name(root));  
            if(diePath.filename() == filePath.filename()) {
                auto& lt = cu.get_line_table();
                for(auto entry : lt) {
                    if(entry.line == line && entry.is_stmt ) {
                        auto addr = std::bit_cast<intptr_t>(entry.address);
                        //return setBreakpointAtAddress(addr);
                        filepathAndAddr.push_back({dwarf::at_name(root), addr});
                        break;
                    }
                }
            }
        }
    }
    auto optionalAddr = handleDuplicateFilenames(file, filepathAndAddr);
    if(optionalAddr)
        return setBreakpointAtAddress(optionalAddr.value());
    return {addrToBp_.end(), false};
}

std::optional<intptr_t>Debugger::handleDuplicateFilenames( const std::string_view filepath, const std::vector<std::pair<std::string, intptr_t>>& fpAndAddr)  {
    if(fpAndAddr.empty()) return std::nullopt;
    else if(fpAndAddr.size() == 1) {     //comment this else-if block out when testing
        auto addr = fpAndAddr[0].second;
        return std::bit_cast<intptr_t>(addLoadAddress(addr));
    }
    std::cout << "\n[info] Multiple matches found for '" << filepath << "':\n";
    int count = 0;
    for(const auto&[fp, addr] : fpAndAddr) {
        std::cout << std::dec << "\t[" << count++ << "] " << fp
            << " at 0x" << std::hex << std::uppercase << addr 
            << " (0x" << addLoadAddress(addr) << ")\n";
    }
 
    std::cout << "\n[info] Select one to set a breakpoint (or abort): ";
    std::string selection;
    std::cin >> selection;
    count = 0;
    uint64_t index;
    if(validDecStol(index, selection) && index < fpAndAddr.size()) {
        auto addr = std::bit_cast<uint64_t>(fpAndAddr[index].second);
        return std::bit_cast<intptr_t>(addLoadAddress(addr));
    }
    return std::nullopt;
}





void Debugger::removeBreakpoint(intptr_t address) {
    if(retAddrFromMain_ && retAddrFromMain_->getAddr() == address && retAddrFromMain_->isEnabled()) {
        std::cerr << "[warning] Attemping to remove breakpoint at the return address of main. "
            "Continue? ";
        
        //Let user decide if breakpoint should be removed
        if(promptYesOrNo()) {
            std::cerr << "[warning] Breakpoint at return address of main has been removed!\n";
            retAddrFromMain_ = nullptr;
        }
        else {
            std::cout << "[debug] Aborting breakpoint removal\n";
            return;
        }
    }

    auto it = addrToBp_.find(address);
    if(it != addrToBp_.end()) {
        if(it->second.isEnabled()) {
            it->second.disable();
        }
        addrToBp_.erase(address);
    }
}

void Debugger::removeBreakpoint(std::unordered_map<intptr_t, Breakpoint>::iterator it) {
    if(retAddrFromMain_ && retAddrFromMain_ == &(it->second) && retAddrFromMain_->isEnabled()) {
        std::cerr << "[warning] Attemping to remove breakpoint at the return address of main. "
            "Continue? ";

        //Let user decide if breakpoint should be removed
        if(promptYesOrNo()) {
            std::cerr << "[warning] Breakpoint at return address of main has been removed!\n";
            retAddrFromMain_ = nullptr;
        }
        else {
            std::cout << "[debug] Aborting breakpoint removal\n";
            return;
        }
    }

    if(it != addrToBp_.end()) {
        if(it->second.isEnabled()) {
            it->second.disable();
        }
        addrToBp_.erase(it);
    }
}




void Debugger::dumpBreakpoints() const {
    if(addrToBp_.empty()) {
        std::cout << "[error] No breakpoints set!";
        return;
    }
    int count = 1;
    for(auto& it : addrToBp_) {
        auto addr = std::bit_cast<uint64_t>(it.first);

        std::cout << "\n" << std::dec 
            << (&it.second == retAddrFromMain_ ? "Main Return —" : std::to_string(count++) + ")") 
            << " 0x" << std::hex << std::uppercase << addr 
            << " (0x" << offsetLoadAddress(addr) << ")"
            " [" << ((it.second.isEnabled()) ? "enabled" : "disabled") << "]";
    }
    std::cout << std::endl;
}