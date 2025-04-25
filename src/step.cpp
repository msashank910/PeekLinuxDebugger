#include "../include/debugger.h"
#include "../include/util.h"
#include "../include/state.h"
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
using util::promptYesOrNo;
using namespace state;

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
    
    //this if statement will never hit!
    if(retAddrFromMain_ && it != addrToBp_.end() && retAddrFromMain_ == &(it->second)) {    //time to exit! 
        std::cout << "[debug] In Debugger::singleStepBreakpointCheck() - Main return Breakpoint hit!\n";
            //"[debug] Preparing to cleanup and exit...\n";
        if(state_ == Child::running) state_ = Child::finish;
        else if(state_ == Child::faulting) state_ = Child::force_detach;
        return;
    }
    else if(it == addrToBp_.end()) {
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
    if(isTerminated(state_)) return false;
    auto chunk = memMap_.getChunkFromAddr(getPC());
    
    if (chunk && (!chunk->get().isPathtypeExec() || !itr)) {
        if(shouldStep) {
            auto c = chunk->get();
            std::cout << "[warning] Memory Space " << MemoryMap::getNameFromPath(c.path) 
            << " has no dwarf info, stepping through instructions instead!\n";
            singleStepBreakpointCheck(); 
        }
        return false;
    }
    else if(isExecuting(state_) && !chunk) { return false; }
    else if(!chunk) {
        std::cerr << "[info] PC at 0x" << std::hex << std::uppercase << getPC() 
            << " is not in any mapped memory region. Terminating debugger session - "
            "Thank you for using Peek\n";
        state_ = Child::kill;
        return false; 
    }
    return true;
}

/*
    Read and write stackSnapshot() functions each take a reference to a vector stack of type 
    std::pair<uint64_t, bool>. The first value in the pair represents the memory at the stack location 
    and the second value represents if the read/write was a success. For the read function, the stack 
    vector is passed in as an empty vector (or overwritten if there was a value there) and populated 
    through readMemory() calls at rsp offsets. Likewise, the write memory takes a populated vector and 
    writes to memory each index 'i' to its respective rsp offset via the formula:

    stack[i].address = rsp + 8*(i+1). 

    The boolean at each index is populated by the return value of chunk.isReadable() and chunk.isWriteable(). 
    If an index wasn't read, readStackSnapshot() will return false and set each index not read to {0, false}. 
    If an index couldn't be written, writeStackSnapShot() will return a false and set the boolean of each 
    unwritten index to false. Note: an unread index will not necessarily fail writeStackSnapshot() as it will 
    ignore values already false.
*/
bool Debugger::readStackSnapshot(std::vector<std::pair<uint64_t, bool>>& stack, size_t bytes) {
    std::vector<std::pair<uint64_t, bool>> tempStack;
    size_t elements = bytes/8;
    tempStack.reserve(elements);
    uint64_t rspOffset = getRegisterValue(pid_, Reg::rsp);
    bool everyReadSucceeded = true;

    // std::cout << "\nTEST - readStackSnapshot()\n";
    // std::cout << "--------------------------------------------------------\n";
    // std::cout << "rsp = 0x" << std::hex << std::uppercase << rspOffset 
    //     << std::endl;

    for(size_t i = 0; i < elements; i++) {
        rspOffset += 8;
        auto chunk = memMap_.getChunkFromAddr(rspOffset);
        if(chunk && chunk.value().get().canRead()) {
            uint64_t data;
            readMemory(rspOffset, data);
            tempStack.push_back({data, true});
        }
        else {
            tempStack.push_back({0, false});
            everyReadSucceeded = false;
        }
        // std::cout << "Stack at rsp + 0x" << std::hex << std::uppercase << rspOffset << ": 0x"
        //     << tempStack[i].first << " (was read: " << (tempStack[i].second ? "true" : "false") << ")\n";
    }
    stack = tempStack;
    return everyReadSucceeded;
}

//bytes are not really needed, can be used for stack.size() vs bytes messages
bool Debugger::writeStackSnapshot(std::vector<std::pair<uint64_t, bool>>& stack, size_t bytes) {
    bool everyWriteSucceeded = true;
    uint64_t rspOffset = getRegisterValue(pid_, Reg::rsp);
    // std::cout << "\nTEST - writeStackSnapshot()\n";
    // std::cout << "--------------------------------------------------------\n";
    // std::cout << "rsp = 0x" << std::hex << std::uppercase << rspOffset 
    //     << std::endl;
    // std::cout << (stack.empty() ? "[critical] STACK IS EMPTY" : "") << "\n";
    for(auto&[data, successfulRead] : stack) {
        rspOffset += 8;
        auto chunk = memMap_.getChunkFromAddr(rspOffset);
        if(successfulRead) {
            if(chunk && chunk.value().get().canWrite()) {
                writeMemory(rspOffset, data);
            }
            else {  //function only fails when cannot write to a read place
                successfulRead = false;
                everyWriteSucceeded = false;
            }
            // std::cout << "Stack at rsp + 0x" << std::hex << std::uppercase << rspOffset << ": 0x"
            //     << data << " (was written: " << (successfulRead ? "true" : "false") << ")\n";
        }
        //no failure is marked for not writing to an unread area
        
    }
    return everyWriteSucceeded;
}





void Debugger::stepOut() {
    auto itr = getLineEntryFromPC(getPCOffsetAddress());
    if(!validMemoryRegionShouldStep(itr, false)) {
        std::cerr << "[warning] No DWARF info found — return may not land in calling function.\n";
    }
    
    auto retAddrLocation = getRegisterValue(pid_, Reg::rbp) + 8;
    auto chunk = memMap_.getChunkFromAddr(retAddrLocation);
    if(!chunk || !chunk.value().get().canRead()) {
        std::cerr << "[warning] Cannot step over with an invalid frame pointer or "
            "return address. Stepping-in instead.\n";
        return stepIn();
    }
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


void Debugger::stepIn() { 
    auto pcOffset = getPCOffsetAddress();
    auto start = pcOffset;
    auto itr = getLineEntryFromPC(pcOffset);

    if(!validMemoryRegionShouldStep(itr, true)) return;
    
    user_regs_struct regs;
    std::vector<std::pair<uint64_t, bool>> stack;
    bool readRegs = getAllRegisterValues(pid_, regs);
    bool readStack = readStackSnapshot(stack);
    auto currFunc = getFunctionFromPCOffset(pcOffset);

    unsigned sourceLine = itr.value()->line;
    //Check if there is a line entry and if the line number has changed.
    while((itr = getLineEntryFromPC(pcOffset)) && (itr.value()->line == sourceLine)) {
        singleStepBreakpointCheck();
        pcOffset = getPCOffsetAddress();
    }
    
    if(start == pcOffset) {
        std::cerr << "[warning] Step-In has made no progress. Aborting function.\n";
        return;
    }
    else if(itr) {
        if(currFunc && !dwarf::die_pc_range(currFunc.value()).contains(pcOffset)) {
            stepIn();
        }
        return;
    }
    //The line iterator does not exist, meaning we are in a region with no DWARF info
    //reaching this point means that we may need to do a state reversal
    bool shouldRevertState = false;
    bool didRevertRegs = false;
    auto chunk = memMap_.getChunkFromAddr(getPC());
    auto memoryLocation = (chunk ? MemoryMap::getNameFromPath(chunk.value().get().path) : "unmapped memory");
             
    if(!itr && isExecuting(state_)) {
        std::cout << "[info] Entered region with no DWARF info ("
            << memoryLocation << "). Revert state and step-over? ";

        //Let user determine if state should be reversed
        shouldRevertState = promptYesOrNo();
    }

    if(shouldRevertState && readRegs && (didRevertRegs = setAllRegisterValues(pid_, regs))) {
        std::cout << "[debug] Reverting memory state and stepping over...\n";
        if(!readStack) {
            for(size_t i = 0; i < stack.size(); i++) {  //warnings could be simplified
                if(!stack[i].second) {
                    std::cerr << "[warning] Address at 0x" << std::hex << std::uppercase
                        << (regs.rsp + 8 * (i+1)) << " was not read!\n";
                }
            }
        }
        bool wroteToStack = writeStackSnapshot(stack);
        if(!wroteToStack) {
            for(size_t i = 0; i < stack.size(); i++) {  //warnings could be simplified
                if(!stack[i].second) {
                    std::cerr << "[warning] Address at 0x" << std::hex << std::uppercase
                        << (regs.rsp + 8 * (i+1)) << " was not written!\n";
                }
            }
        }
        stepOver();
    }
    else if (shouldRevertState && readRegs && !didRevertRegs) {
        std::cout << "[critical] Registers were not restored properly, proceed with caution. " 
            "Cannot step-over memory region with no DWARF info.\n";
    }
    else if (shouldRevertState) {
        std::cout << "[warning] Registers could not be restored! Cannot step-over memory region with no DWARF"
            " info.\n";
    }
    else if(!shouldRevertState) {   
        std::cout << "[warning] Region can only be stepped through by instruction.\n";
    }
}


void Debugger::stepOver() {
    auto pcOffset = getPCOffsetAddress();
    auto currEntry = getLineEntryFromPC(pcOffset);

    //Return early if there is no DWARF info
    if(!validMemoryRegionShouldStep(currEntry, true)) return;
    auto func = getFunctionFromPCOffset(pcOffset);
    auto startAddr = addLoadAddress(currEntry.value()->address);
    auto retAddrLocation = getRegisterValue(pid_, Reg::rbp) + 8;
    auto chunk = memMap_.getChunkFromAddr(retAddrLocation);

    //You may still have DWARF info in an invalid, non-user-defined function. This check will handle that.
    if(!func) {
        std::cerr << "[warning] Cannot step over line in non-user-defined function. Stepping-in instead.\n";
        return stepIn();
    }
    else if(!chunk || !chunk.value().get().canRead()) {
        std::cerr << "[warning] Cannot step over with an invalid frame pointer or "
            "return address. Stepping-in instead.\n";
        return stepIn();
    }

    //All code from here assumes you are in a user-defined function with DWARF info.
    auto low = dwarf::at_low_pc(func.value());
    auto high = dwarf::at_high_pc(func.value());
    auto lowEntry = getLineEntryFromPC(low);
    if(!lowEntry) {
        std::cout << "[debug] Function cannot be resolved in line table. Single stepping...\n";
        singleStepBreakpointCheck();
        return;
    }
    
    std::vector<std::pair<std::intptr_t, bool>> addrshouldRemove;
    unsigned startLine = currEntry.value()->line;

    for(auto val = lowEntry.value(); val->address < high; val++) {
        auto addr = addLoadAddress(val->address);
        
        //Prior to placing a bp, check if the address is the same as starting address
        //The if-statement is appended such that stepOver() will skip the current line completely
        if(addr == startAddr || val->line == startLine || !val->is_stmt) continue; 
        auto [it, inserted] = setBreakpointAtAddress(std::bit_cast<intptr_t>(addr));

        if(it == addrToBp_.end()) continue;
        else if(inserted) {
            addrshouldRemove.push_back({it->first, true});
        }
        else if(!it->second.isEnabled()) {
            addrshouldRemove.push_back({it->first, false});
        }
    }
    
    uint64_t retAddr;
    readMemory(retAddrLocation, retAddr);
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






void Debugger::skipUnsafeInstruction(const size_t bytes) {
    auto rip = getRegisterValue(pid_, Reg::rip);
    auto newRip = rip + static_cast<uint64_t>(bytes);

    if(!setRegisterValue(pid_, Reg::rip, newRip)) {
        std::cerr << "[warning] RIP was not set properly\n";
        return;
    }
    else if(uint64_t setRip = getRegisterValue(pid_, Reg::rip); setRip != newRip) {
        std::cerr << "[critical] Read RIP does not match with set RIP (0x"
            << std::uppercase << std::hex << setRip << "). Proceed with caution.\n";
    }

    auto ripChunk = memMap_.getChunkFromAddr(rip);
    auto newRipChunk = memMap_.getChunkFromAddr(newRip);
    std::string ripChunkMem = "Unmapped Memory";
    std::string newRipChunkMem = "Unmapped Memory";

    if(ripChunk) {
        ripChunkMem = MemoryMap::getNameFromPath(ripChunk.value().get().path);
    }
    if(newRipChunk) {
        newRipChunkMem = MemoryMap::getNameFromPath(newRipChunk.value().get().path);
    }

    std::cout << "[debug] Rip has been set 0x" << std::hex << std::uppercase << rip
        << " (0x" << offsetLoadAddress(rip) << ") --> 0x" << (newRip) 
        << " (0x" << offsetLoadAddress(newRip) << ")\n"
        "[debug] " << ripChunkMem << " --> " << newRipChunkMem << "\n";
}


void Debugger::jumpToInstruction(const uint64_t newRip) {
    auto rip = getRegisterValue(pid_, Reg::rip);

    if(!setRegisterValue(pid_, Reg::rip, newRip)) {
        std::cerr << "[warning] RIP was not set properly\n";
        return;
    }
    else if(uint64_t setRip = getRegisterValue(pid_, Reg::rip); setRip != newRip) {
        std::cerr << "[critical] Read RIP does not match with set RIP (0x"
            << std::uppercase << std::hex << setRip << "). Proceed with caution.\n";
    }

    auto ripChunk = memMap_.getChunkFromAddr(rip);
    auto newRipChunk = memMap_.getChunkFromAddr(newRip);
    std::string ripChunkMem = "Unmapped Memory";
    std::string newRipChunkMem = "";

    if(ripChunk) {
        ripChunkMem = MemoryMap::getNameFromPath(ripChunk.value().get().path);
    }
    if(newRipChunk) {
        newRipChunkMem = MemoryMap::getNameFromPath(newRipChunk.value().get().path);
    }
    else {
        std::cerr << "[warning] Cannot jump to unmapped memory.";
        return;
    }

    std::cout << "[debug] Rip has been set 0x" << std::hex << std::uppercase << rip
        << " (0x" << offsetLoadAddress(rip) << ") --> 0x" << (newRip) 
        << " (0x" << offsetLoadAddress(newRip) << ")\n"
        "[debug] " << ripChunkMem << " --> " << newRipChunkMem << "\n";
}