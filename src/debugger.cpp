#include "../include/debugger.h"
#include "../include/register.h"
#include "../include/breakpoint.h"
#include "../include/memorymap.h"

#include <dwarf/dwarf++.hh>
#include <elf/elf++.hh>

#include <iostream>
#include <fstream>
#include <sys/ptrace.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <cstring>
#include <bit>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <cmath>


//Use reg namespace
using namespace reg;

//Debugger Member Functions
Debugger::Debugger(pid_t pid, std::string progName) : pid_(pid), progName_(std::move(progName)), 
    loadAddress_(0), exit_(false), context_(2) {
    auto fd = open(progName_.c_str(), O_RDONLY);
    
    elf_ = elf::elf(elf::create_mmap_loader(fd));
    dwarf_ = dwarf::dwarf(dwarf::elf::create_loader(elf_));
}

void Debugger::initializeMemoryMapAndLoadAddress() {
    if(elf_.get_hdr().type == elf::et::dyn) {
        
        std::unique_ptr<char, decltype(&free)> ptr(realpath(progName_.c_str(), nullptr), free);
        if(!ptr) {
            throw std::logic_error(std::string("\n[fatal] In Debugger::initializeMemoryMapAndLoadAddress - ") 
                + " Invalid or nonexistant program name, realpath() failed!\n");
        }

        std::string absFilePath = ptr.get();
        memMap_ = MemoryMap(pid_, absFilePath);

        std::ifstream file;
        file.open("/proc/" + std::to_string(getPID()) + "/maps");
        if(!file.is_open()) {
            throw std::runtime_error(std::string("\n[fatal] In Debugger::initializeMemoryMapAndLoadAddress - ")
                + "/proc/pid/maps could not be opened! Check permisions.\n");
        }
        std::string addr = "";
        std::string possibleFilePath = "";

        std::getline(file, addr, '-');
        std::getline(file, possibleFilePath, '\n');
        possibleFilePath.erase(0, possibleFilePath.find_last_of(" \t") + 1);

        while(possibleFilePath != absFilePath && std::getline(file, addr, '-')) {
            std::getline(file, possibleFilePath, '\n');
            possibleFilePath.erase(0, possibleFilePath.find_last_of(" \t") + 1);
        } 
        
        file.close();
        loadAddress_ = std::bit_cast<uint64_t>(std::stol(addr, nullptr, 16));

    }
}

uint64_t Debugger::offsetLoadAddress(uint64_t addr) const { return addr - loadAddress_;}

uint64_t Debugger::addLoadAddress(uint64_t addr) const { return addr + loadAddress_;}


uint64_t Debugger::getPCOffsetAddress() const {return offsetLoadAddress(getPC());}

pid_t Debugger::getPID() const {return pid_;}

uint64_t Debugger::getPC() const { return getRegisterValue(pid_, Reg::rip); }

bool Debugger::setPC(uint64_t val) { return setRegisterValue(pid_, Reg::rip, val); }

uint8_t Debugger::getContext() const {return context_;}

void Debugger::setContext(uint8_t context) {context_ = context;}


std::pair<std::unordered_map<intptr_t, Breakpoint>::iterator, bool>
     Debugger::setBreakpointAtAddress(std::intptr_t address) {
    auto [it, inserted] = addrToBp_.emplace(address, Breakpoint(pid_, address));

    if(inserted) {
        if(!it->second.enable()) {
            std::cerr << "[error] Invalid Memory Address!";
            addrToBp_.erase(it);
            return {addrToBp_.end(), false};
        }
    }

    return {it, inserted};
}

void Debugger::dumpBreakpoints() const {
    if(addrToBp_.empty()) {
        std::cout << "[error] No breakpoints set!";
        return;
    }
    int count = 1;
    for(auto& it : addrToBp_) {
        auto addr = std::bit_cast<uint64_t>(it.first);

        std::cout << "\n" << std::dec << count << ") 0x" << std::hex << std::uppercase << addr 
            << " (0x" << offsetLoadAddress(addr) << ")"
            << " [" << ((it.second.isEnabled()) ? "enabled" : "disabled") << "]";
        ++count;
    }
    std::cout << std::endl;
}



void Debugger::continueExecution() {
    stepOverBreakpoint();
    ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
    waitForSignal();

}

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

    if(it == addrToBp_.end()) {
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

void Debugger::removeBreakpoint(intptr_t address) {
    auto it = addrToBp_.find(address);
    
    if(it != addrToBp_.end()) {
        if(it->second.isEnabled()) {
            it->second.disable();
        }
        addrToBp_.erase(address);
    }
}

void Debugger::removeBreakpoint(std::unordered_map<intptr_t, Breakpoint>::iterator it) {
    if(it != addrToBp_.end()) {
        if(it->second.isEnabled()) {
            it->second.disable();
        }
        addrToBp_.erase(it);
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
    
    unsigned sourceLine = itr.value()->line;
    // user_regs_struct regValStruct;
    // auto rawVals = getAllRegisterValues(pid_, regValStruct);
    // bool retFromMain = false;

    while((itr = getLineEntryFromPC(pcOffset)) && itr.value()->line == sourceLine) {
        singleStepBreakpointCheck();
        pcOffset = getPCOffsetAddress();
    }

    // if(!itr) {
    //     std::cout << "Stepped into region with no dwarf info, single-stepping instead\n";
    //     //singleStep();  
    //     //In future --> resolve end of main, check if stepin is in shared library or out of main
    //     //If out of main, resort to single stepping, if in shared library:
    //         //-> revert registers, then stepOver()  
    // }
    //else printSourceAtPC();
    // printSourceAtPC();  //handles valid iterator in memory space check
}

void Debugger::stepOver() {
    auto pcOffset = getPCOffsetAddress();
    auto itr = getLineEntryFromPC(pcOffset);

    if(!validMemoryRegionShouldStep(itr, true)) return;

    auto func = getFunctionFromPC(pcOffset);
    auto startAddr = addLoadAddress(itr.value()->address);

    auto low = dwarf::at_low_pc(func);
    auto high = dwarf::at_high_pc(func);
    auto curr = getLineEntryFromPC(low);

    // if(!curr) {
    //     std::cout << "Function cannot be resolved in line table. Single stepping...\n";
    //     singleStepBreakpointCheck();
    //     return;
    // }
    

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


dwarf::die Debugger::getFunctionFromPC(uint64_t pc) const {
    for(const auto& cu : dwarf_.compilation_units()) {
        if(dwarf::die_pc_range(cu.root()).contains(pc)) {
            for(const auto& die : cu.root()) {
                if(die.tag == dwarf::DW_TAG::subprogram &&
                    (die.has(dwarf::DW_AT::low_pc) || die.has(dwarf::DW_AT::ranges))) {
                        if(dwarf::die_pc_range(die).contains(pc)) return die;
                }
            }
        }
    }
    throw std::out_of_range(std::string("\n[fatal] In Debugger::getFunctionFromPC() - ") 
        + "Function not found for pc. Something is definitely wrong!\n");
}

std::optional<dwarf::line_table::iterator> Debugger::getLineEntryFromPC(uint64_t pc) const {
    for(const auto& cu : dwarf_.compilation_units()) {
        if(dwarf::die_pc_range(cu.root()).contains(pc)) {
            const auto& lineTable = cu.get_line_table();
            auto lineEntryItr = lineTable.find_address(pc);
            if(lineEntryItr != lineTable.end()) return lineEntryItr;
            //else throw std::out_of_range("Line Entry not found in table!\n");
        }   //maybe turn to optional, need ways of distinguishing both error types
    }
    //throw std::out_of_range("PC not found in any line table!\n");
    return std::nullopt;
}

void Debugger::printSource(const std::string fileName, unsigned line, uint8_t numOfContextLines) const {
    unsigned start = (line > numOfContextLines) ? line - numOfContextLines : 1;
    unsigned end = line + numOfContextLines + 1;  //could optimize formula
    
    std::ifstream file;
    file.open(fileName, std::ios::in);

    if(!file.is_open()) {
        throw std::runtime_error(std::string("\n[fatal] In Debugger::printSource() - ")
            + "File could not be opened! Check permisions.\n");
    }
    
    std::string buffer = "";

    //calculate uniform spacing based on digits floor(log10(n)) + 1 --> number of digits in n
    const std::string spacing(static_cast<int>(log10(end)) + 1, ' ');
    
    unsigned index = 1;
    for(; index < start; index++) {
        std::getline(file, buffer, '\n');
    }
    std::cout << "\n";
    while(getline(file, buffer, '\n') && index < end) {
        std::cout << std::dec << (index == line ? "> " : "  ") << index << spacing << buffer << "\n";
        ++index;
    }

    file.close();
    std::cout << std::endl; //flush buffer and extra newline just in case
}

void Debugger::printSourceAtPC() {
    auto itr = getLineEntryFromPC(getPCOffsetAddress());
    if(!validMemoryRegionShouldStep(itr, false)) return;
    
    auto lineEntryItr = itr.value();
    printSource(lineEntryItr->file->path, lineEntryItr->line, context_);
}

void Debugger::printMemoryLocationAtPC() const {
    if(exit_ == true) return;

    auto pc = getPC();
    auto pcOffset = offsetLoadAddress(pc);
    auto chunk = memMap_.getChunkFromAddr(pc);
    
    std::cout << std::hex << std::uppercase << "[debug] Currently at PC: 0x" 
        << pc << " (0x" << pcOffset << ") --> "
        << (chunk ? MemoryMap::getFileNameFromChunk(chunk.value()) : "Unmapped Memory")
        << "\n";
}

void Debugger::waitForSignal() {
    int options = 0;
    int wait_status;
    errno = 0;
    //std::cout << "Getting ready to wait!!\n";

    if(waitpid(pid_, &wait_status, options) == -1) {
        //std::cerr << "ptrace error: " << strerror(errno) << ".\n WaitPid failed!\n";
        throw std::runtime_error(std::string("[fatal] In Debugger::waitForSignal() - ")
            + "ptrace error: " + std::string(strerror(errno)) + ".\n Waitpid failed!\n");
    }

    if(WIFEXITED(wait_status)) {
        std::cout << "\n[info] Child process has complete normally. Thank you for using Peek!" << std::endl;
        exit_ = true;
        return;
    }
    else if(WIFSIGNALED(wait_status)) {
        std::cout << "\n[info] Child process has terminated abnormally! (Thank you for using Peek)" << std::endl;
        exit_ = true;
        return;
    }

    if(memMap_.initialized() && !exit_) memMap_.reload();

    auto signal = getSignalInfo();
    switch(signal.si_signo) {
        case SIGTRAP:
            handleSIGTRAP(signal);
            return;
        case SIGSEGV:
            std::cerr << "[critical] Segmentation Error: " << signal.si_signo << ", Reason: " << signal.si_code << "\n";
            break;
        default:
            std::cerr << "[warning] Signal: " << signal.si_signo << ", Reason: " << signal.si_code << "\n";
            break;
    }
    
    printSourceAtPC();
}

siginfo_t Debugger::getSignalInfo() const {
    errno = 0;
    siginfo_t data;
    if(ptrace(PTRACE_GETSIGINFO, pid_, nullptr, &data) == -1) {
        throw std::runtime_error("[fatal] In Debugger::getSignalInfo() - ptrace failure: " 
            + std::string(strerror(errno)) + ".\n");
    }
    return data;
}

void Debugger::handleSIGTRAP(siginfo_t signal) {
    switch(signal.si_code) {
        case SI_KERNEL:
        case TRAP_BRKPT: {
            setPC(getPC() - 1); //pc is being decremented for stepOverBreakpoint()
            return;
        }
        case 0:
            return;
        case TRAP_TRACE:
            //std::cout << "Single-Stepping\n";
            return;
    
        default:
            std::cout << "[warning] Unknown SIGTRAP code: " << signal.si_code << "\n";
    }
}