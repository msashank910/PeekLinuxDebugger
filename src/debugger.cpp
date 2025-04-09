#include "../include/debugger.h"
#include "../include/util.h"
#include "../include/register.h"
#include "../include/breakpoint.h"
#include "../include/memorymap.h"
#include "../include/symbolmap.h"

#include <dwarf/dwarf++.hh>
#include <elf/elf++.hh>

#include <iostream>
#include <unordered_map>
#include <fstream>
#include <sys/ptrace.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <string>
#include <cstring>
#include <bit>
#include <memory>
#include <utility>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <optional>
#include <cctype>



//Use reg namespace
using namespace reg;
// using util::validDecStol;

//Debugger Member Functions
Debugger::Debugger(pid_t pid, std::string progName) : pid_(pid), progName_(std::move(progName)), 
    loadAddress_(0), exit_(false), context_(2), retAddrFromMain_(nullptr) {
    auto fd = open(progName_.c_str(), O_RDONLY);
    
    elf_ = elf::elf(elf::create_mmap_loader(fd));
    dwarf_ = dwarf::dwarf(dwarf::elf::create_loader(elf_));
    
    close(fd);
}

void Debugger::initialize() {
    initializeMapsAndLoadAddress(); //initialize mem map, sym map and load addr from /proc/pid/maps
    initializeFunctionDies();       //initialize user function DIEs from dwarf info

    //sets a breakpoint on the first valid entry of int main(), skips lineTable check in setBreakpoint()
    auto [mainBp, success] = setBreakpointAtFunctionName("main");
    if(!success) {
        std::cerr << "\n[critical] Could not set a breakpoint on first valid line in main().\n"
        "[critical] Could not set a breakpoint on the return address of main().\n";
        return;
    }        
    continueExecution();    
    removeBreakpoint(mainBp);

    //sets a breakpoint on the return address of int main(), skips lineTable check in setBreakpoint()
    auto fp = getRegisterValue(pid_, Reg::rbp); 
    uint64_t retAddr;
    readMemory(fp + 8, retAddr); 
    auto[it, inserted] = setBreakpointAtAddress(std::bit_cast<intptr_t>(retAddr));

    if(!inserted || it == addrToBp_.end()) {
        std::cerr << "\n[critical] Could not set a breakpoint on the return address of main().\n";
        retAddrFromMain_ = nullptr;        //redundant, but for safety
    }
    else retAddrFromMain_ = &it->second;
}

void Debugger::initializeMapsAndLoadAddress() {
    if(elf_.get_hdr().type == elf::et::dyn) {
        std::unique_ptr<char, decltype(&free)> ptr(realpath(progName_.c_str(), nullptr), free);
        if(!ptr) {
            throw std::logic_error(std::string("\n[fatal] In Debugger::initializeMemoryMapAndLoadAddress() - ") 
                + " Invalid or nonexistant program name, realpath() failed!\n");
        }

        std::string absFilePath = ptr.get();
        memMap_ = MemoryMap(pid_, absFilePath);
        uint64_t foundLoadAddress = UINT64_MAX;

        for(auto& chunk : memMap_.getChunks()) {
            if(chunk.isPathtypeExec()) {
                foundLoadAddress = std::min(chunk.addrLow, foundLoadAddress);
            }
        }
        if(foundLoadAddress == UINT64_MAX) {
            throw std::runtime_error("\n[fatal] In Debugger::initializeMemoryMapAndLoadAddress() - "
                "Load address could not be resolved from /proc/pid/maps! Check permisions.\n");
        }
        loadAddress_ = foundLoadAddress;

    }
    symMap_ = SymbolMap(elf_, loadAddress_);

}

void Debugger::cleanup() {
    //handle breakpoint cleanup
    retAddrFromMain_ = nullptr;

    for(auto& it : addrToBp_) {
        //std::cerr << "DEBUG: disabling bp!\n";
        if(it.second.isEnabled()) it.second.disable();
    }
    // dumpBreakpoints();
    // dumpFunctionDies();
    // dumpRegisters();
    // memMap_.dumpChunks();
    // symMap_.dumpSymbolCache();

    std::cout << "[debug] Cleanup has been completed. Press [Enter] to exit the debugger. ";
    std::string debugString;
    std::getline(std::cin, debugString);
    //detach from process
   // ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);

}

void Debugger::continueExecution() {
    stepOverBreakpoint();
    ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
    waitForSignal();

}

uint64_t Debugger::offsetLoadAddress(uint64_t addr) const { return addr - loadAddress_;}
uint64_t Debugger::addLoadAddress(uint64_t addr) const { return addr + loadAddress_;}
uint64_t Debugger::getPCOffsetAddress() const {return offsetLoadAddress(getPC());}
pid_t Debugger::getPID() const {return pid_;}
uint64_t Debugger::getPC() const { return getRegisterValue(pid_, Reg::rip); }
bool Debugger::setPC(uint64_t val) { return setRegisterValue(pid_, Reg::rip, val); }
uint8_t Debugger::getContext() const {return context_;}
void Debugger::setContext(uint8_t context) {context_ = context;}





/*
    Dies cannot be stored by references as they are results of temporary objects from the iterators.
    Storing them as references causes UB in dangling references (reference to pointer from temp itr).
    Solution --> Store them as copies (even though they are a bit expensive)
        or store them as compilation unit pointers and offset (what i'm doing here)
*/
void Debugger::initializeFunctionDies() {
    for(const auto& cu : dwarf_.compilation_units()) {
        std::vector<dwarf::section_offset> offset;
        for(auto& die : cu.root()) {
            if(die.tag == dwarf::DW_TAG::subprogram && die.has(dwarf::DW_AT::name) && 
                (die.has(dwarf::DW_AT::low_pc) || die.has(dwarf::DW_AT::ranges))) {
                std::string name = dwarf::at_name(die);
                if(name.length() > 1 && name[0] == '_' && (name[1] == '_' || std::isupper(name[1]))) continue;
                offset.push_back(die.get_unit_offset());
            }
        }
        if(!offset.empty()) functionDies_[&cu] = std::move(offset);
    }
    if(functionDies_.empty())
        throw std::out_of_range(std::string("\n[fatal] In Debugger::initializeFunctionDies() - ") 
            + "No functions found. Something is definitely wrong!\n");
}


void Debugger::dumpFunctionDies() {
    int cuCount = 0;
    int totalFunctionCount = 0;
    std::cout << "--------------------------------------------------------\n";
    for(const auto&[cu, offset] : functionDies_) {
        ++cuCount;
        std::string unit = "";
        auto& root = cu->root();
        if(root.has(dwarf::DW_AT::name)) {
            unit = dwarf::at_name(root);
        }
        int offsetCount = 0;
        for(auto& die : root) {
            if(offset[offsetCount] == die.get_unit_offset()) {
                ++totalFunctionCount;
                std::cout << std::dec << "(" << cuCount << ") "
                    << unit << " --> " << ++offsetCount << ") "
                    << (die.has(dwarf::DW_AT::name) ? dwarf::at_name(die) : "unnamed die")
                    << "\n";
            }
        }
    }
    std::cout << "--------------------------------------------------------\n"
         << "[debug] Total functions: " << totalFunctionCount << "\n";
}


dwarf::die Debugger::getFunctionFromPCOffset(uint64_t pc) const {
    for(auto& [cu, offset] : functionDies_) {
        if(dwarf::die_pc_range(cu->root()).contains(pc)) {
            int count = 0;
            for(const auto& die : cu->root()) {
                if(offset[count] == die.get_unit_offset()) {
                    ++count;
                    if(dwarf::die_pc_range(die).contains(pc)) return die;
                }
            }
        }
    }
    throw std::out_of_range(std::string("\n[fatal] In Debugger::getFunctionFromPCOffset() - ") 
        + "Function not found for pc. Something is definitely wrong!\n");
}

std::optional<dwarf::line_table::iterator> Debugger::getLineEntryFromPC(uint64_t pc) const {
    for(const auto& cu : dwarf_.compilation_units()) {
        if(dwarf::die_pc_range(cu.root()).contains(pc)) {
            const auto& lineTable = cu.get_line_table();
            auto lineEntryItr = lineTable.find_address(pc);
            if(lineEntryItr != lineTable.end()) return lineEntryItr;
            //else throw std::out_of_range("Line Entry not found in table!\n");
        } 
    }
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
    
    //calculate uniform spacing based on digits floor(log10(n)) + 1 --> number of digits in n
    const std::string spacing(static_cast<int>(log10(end)) + 1, ' ');
    std::string buffer = "";
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

    if(waitpid(pid_, &wait_status, options) == -1) {
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

            if(retAddrFromMain_ && getPC() == std::bit_cast<uint64_t>(retAddrFromMain_->getAddr())) {
                cleanup();
                //std::cerr << "DEBUG: cleanup complete! continuing...\n";
                continueExecution();
            }
            return;
        case SIGSEGV:
            std::cerr << "[critical] Segmentation Error: " << signal.si_signo << ", Reason: " << signal.si_code << "\n";
            break;
        case SIGWINCH:
            std::cout << "[debug] Ignoring SIGWINCH (window resize)\n";
            return waitForSignal();
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
        case TRAP_TRACE:    //single-step
            return;
    
        default:
            std::cout << "[warning] Unknown SIGTRAP code: " << signal.si_code << "\n";
    }
}