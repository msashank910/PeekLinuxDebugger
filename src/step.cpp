#include "../include/debugger.h"
#include "../include/util.h"
#include "../include/register.h"
#include "../include/breakpoint.h"
#include "../include/memorymap.h"

#include <dwarf/dwarf++.hh>

#include <sys/ptrace.h>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <bit>
#include <cstdint>

using namespace reg;


/* Below are all the Debugger class member functions that involve stepping. */


void Debugger::singleStep() {
    errno = 0;
    long res = ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr);

    if(res == -1) {
        throw std::runtime_error("\n[fatal] In Debugger::singleStep() - ptrace error: " 
            + std::string(strerror(errno)) + "..\n[fatal] Single Step Failed!\n");
    }
    waitForSignal();
}

void Debugger::singleStepBreakpointCheck() {  
    auto addr = std::bit_cast<intptr_t>(getPC());
    auto it = addrToBp_.find(addr);
    
    /*if(retAddrFromMain_ && it != addrToBp_.end() && retAddrFromMain_ == &(it->second)) {    //time to exit! 
        cleanup();
        continueExecution();
        return;
    }
    else */if(it == addrToBp_.end()) {
        singleStep();
        return;
    }
    stepOverBreakpoint();
}

void Debugger::stepOverBreakpoint() {
    uint64_t addr = getPC();        //pc points to bp, is altered
    auto it = addrToBp_.find(std::bit_cast<intptr_t>(addr));

    if(it != addrToBp_.end()) {
        if(it->second.isEnabled()) {
            it->second.disable();
            singleStep();
            it->second.enable();
        }
    }
}


bool Debugger::validMemoryRegionShouldStep(std::optional<dwarf::line_table::iterator> itr, bool shouldStep) {
    auto chunk = memMap_.getChunkFromAddr(getPC());

    if (chunk && (!chunk->get().isExec() || !itr)) {
        if(shouldStep) {
            auto c = chunk->get();
            std::cout << "[warning] Memory Space " << MemoryMap::getNameFromPath(c.path) 
            << " has no dwarf info, stepping through instructions instead!\n";
            singleStepBreakpointCheck(); 
        }
        return false;
    }
    else if(exit_ && !chunk) { return false; }
    else if(!chunk) {
        std::cerr << "[info] PC at 0x" << std::hex << std::uppercase << getPC() 
            << " is not in any mapped memory region. Terminating debugger session - "
            << "Thank you for using Peek\n";
        exit_ = true;
        return false; 
    }
    return true;
}





void Debugger::stepOut() {
    auto itr = getLineEntryFromPC(getPCOffsetAddress());

    if(!validMemoryRegionShouldStep(itr, true)) return;
    
    auto retAddrLocation = getRegisterValue(pid_, Reg::rbp) + 8;
    uint64_t retAddr;
    readMemory(retAddrLocation, retAddr);
    auto[it, inserted] = setBreakpointAtAddress(std::bit_cast<intptr_t>(retAddr));

    if(it == addrToBp_.end()) {
        std::cerr << "[warning] Step out failed, invalid return address\n";
        return;
    }
    else if(!inserted) {
        bool prevEnabled = it->second.isEnabled();
        if(!prevEnabled) it->second.enable();
        continueExecution();
        if(!prevEnabled) it->second.disable();
        return;
    }
    continueExecution();
    removeBreakpoint(it);
}


void Debugger::stepIn() {       //need to preface with breakpoint on main when you know how to
    auto pcOffset = getPCOffsetAddress();
    auto itr = getLineEntryFromPC(pcOffset);

    if(!validMemoryRegionShouldStep(itr, true)) return;
    
    user_regs_struct regValStruct;
    auto success = getAllRegisterValues(pid_, regValStruct);

    unsigned sourceLine = itr.value()->line;
    while((itr = getLineEntryFromPC(pcOffset)) && itr.value()->line == sourceLine) {
        singleStepBreakpointCheck();
        pcOffset = getPCOffsetAddress();
    }

    if(!itr) {
        std::cout << "[debug] Stepped into region with no dwarf info, single-stepping instead\n";
        singleStep();  
        //In future --> resolve end of main, check if stepin is in shared library or out of main
        //If out of main, resort to single stepping, if in shared library:
            //-> revert registers, then stepOver()  
    }
    //else printSourceAtPC();
    //printSourceAtPC();  //handles valid iterator in memory space check
}


void Debugger::stepOver() {
    auto pcOffset = getPCOffsetAddress();
    auto itr = getLineEntryFromPC(pcOffset);

    if(!validMemoryRegionShouldStep(itr, true)) return;

    auto func = getFunctionFromPCOffset(pcOffset);
    auto startAddr = addLoadAddress(itr.value()->address);

    auto low = dwarf::at_low_pc(func);
    auto high = dwarf::at_high_pc(func);
    auto curr = getLineEntryFromPC(low);

    if(!curr) {
        std::cout << "[debug] Function cannot be resolved in line table. Single stepping...\n";
        singleStepBreakpointCheck();
        return;
    }
    

    std::vector<std::pair<std::intptr_t, bool>> addrshouldRemove;

    for(auto val = curr.value(); val->address < high; val++) {
   // while(curr && curr.value()->address != high) {
        auto addr = addLoadAddress(val->address);
        if(addr == startAddr) continue; 
        auto [it, inserted] = setBreakpointAtAddress(std::bit_cast<intptr_t>(addr));

        if(it == addrToBp_.end()) continue;
        else if(inserted) {
            addrshouldRemove.push_back({it->first, true});
        }
        else if(!it->second.isEnabled()) {
            addrshouldRemove.push_back({it->first, false});
        }
    }
    
    auto fp = getRegisterValue(pid_, Reg::rbp);
    uint64_t retAddr;
    readMemory(fp + 8, retAddr);
    auto [it, inserted] = setBreakpointAtAddress(std::bit_cast<intptr_t>(retAddr));

    if(it != addrToBp_.end() && inserted) {
        addrshouldRemove.push_back({it->first, true});
    }
    else if(it != addrToBp_.end() && !it->second.isEnabled()) {
        addrshouldRemove.push_back({it->first, false});
    }
    continueExecution();

    for(auto &[addr, shouldRemove] : addrshouldRemove) {
        //will crash if it doesn't exist, but should always exist
        if(shouldRemove) removeBreakpoint(addr);
        else addrToBp_.at(addr).disable();          
    }
}