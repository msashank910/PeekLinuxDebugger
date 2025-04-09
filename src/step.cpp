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
    
    if(retAddrFromMain_ && it != addrToBp_.end() && retAddrFromMain_ == &(it->second)) {    //time to exit! 
        cleanup();
        continueExecution();
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

    for(size_t i = 0; i < elements; i++) {
        rspOffset += 8;
        auto chunk = memMap_.getChunkFromAddr(rspOffset);
        if(chunk && chunk.value().get().canRead()) {
            uint64_t data;
            readMemory(rspOffset, data);
            tempStack[i] = {data, true};
        }
        else {
            tempStack[i] = {0, false};
            everyReadSucceeded = false;
        }
    }
    stack = tempStack;
    return everyReadSucceeded;
}

//bytes are not really needed, can be used for stack.size() vs bytes messages
bool Debugger::writeStackSnapshot(std::vector<std::pair<uint64_t, bool>>& stack, size_t bytes) {
    bool everyWriteSucceeded = true;
    uint64_t rspOffset = getRegisterValue(pid_, Reg::rsp);
    
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
        }
        //no failure is marked for not writing to an unread area
    }
    return everyWriteSucceeded;
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
    
    user_regs_struct regs;
    std::vector<std::pair<uint64_t, bool>> stack;
    bool readRegs = getAllRegisterValues(pid_, regs);
    bool readStack = readStackSnapshot(stack);


    unsigned sourceLine = itr.value()->line;
    while((itr = getLineEntryFromPC(pcOffset)) && itr.value()->line == sourceLine) {
        singleStepBreakpointCheck();
        pcOffset = getPCOffsetAddress();
    }

    bool revertState = false;
    bool revertRegs = false;
    if(!itr && !exit_) {
        std::cout << "[debug] Entered region with no DWARF info. Revert state and step-over? [y/n]: ";
        std::string response = "";
        std::getline(std::cin, response);
        revertState = (response.length() > 0 && (response[0] == 'y' || response[0] == 'Y'));
    }

    if(revertState && readRegs && (revertRegs = setAllRegisterValues(pid_, regs))) {
        std::cout << "[debug] Reverting memory state and stepping over...\n";
        if(!readStack) {
            for(size_t i = 0; i < stack.size(); i++) {
                if(!stack[i].second) {
                    std::cerr << "[warning] Address at 0x" << std::hex << std::uppercase
                        << (regs.rsp + 8 * (i+1)) << " was not read!\n";
                }
            }
        }
        
        bool wroteToStack = writeStackSnapshot(stack);
        if(!wroteToStack) {
            for(size_t i = 0; i < stack.size(); i++) {
                if(!stack[i].second) {
                    std::cerr << "[warning] Address at 0x" << std::hex << std::uppercase
                        << (regs.rsp + 8 * (i+1)) << " was not written!\n";
                }
            }
        }
        stepOver();
        //singleStep();  
        //In future --> resolve end of main, check if stepin is in shared library or out of main
        //If out of main, resort to single stepping, if in shared library:
            //-> revert registers, then stepOver()  
    }
    else if (revertState && !revertRegs) {
        std::cout << "[critical] Registers were not restored properly, proceed with caution." 
            "Cannot step-over memory region with no DWARF info.\n";
    }
    else if (revertState) {
        std::cout << "[warning] Registers could not be restored! Cannot step-over memory region with no DWARF"
            " info.\n";
    }
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