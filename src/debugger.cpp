#include "../include/debugger.h"
#include "../include/util.h"
#include "../include/register.h"
#include "../include/breakpoint.h"
#include "../include/memorymap.h"
#include "../include/symbolmap.h"

#include <dwarf/dwarf++.hh>
#include <elf/elf++.hh>

#include <iostream>
#include <fstream>
#include <filesystem>
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
#include <cctype>



//Use reg namespace
using namespace reg;
using util::validDecStol;

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
            if(chunk.isExec()) {
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

uint64_t Debugger::offsetLoadAddress(uint64_t addr) const { return addr - loadAddress_;}

uint64_t Debugger::addLoadAddress(uint64_t addr) const { return addr + loadAddress_;}


uint64_t Debugger::getPCOffsetAddress() const {return offsetLoadAddress(getPC());}

pid_t Debugger::getPID() const {return pid_;}

uint64_t Debugger::getPC() const { return getRegisterValue(pid_, Reg::rip); }

bool Debugger::setPC(uint64_t val) { return setRegisterValue(pid_, Reg::rip, val); }

uint8_t Debugger::getContext() const {return context_;}

void Debugger::setContext(uint8_t context) {context_ = context;}


std::pair<std::unordered_map<intptr_t, Breakpoint>::iterator, bool>
     Debugger::setBreakpointAtAddress(std::intptr_t address/*, bool skipLineTableCheck*/) {
    
    //First check if it is within an executable memory region or in main process memory space
    auto chunk = memMap_.getChunkFromAddr(address);
    if(!chunk || !(MemoryMap::canExecute(chunk.value().get()) || chunk.value().get().isExec())) {
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

/*
    Handle Duplicate function names. Takes in a const ref to a vector of dies.
*/
std::optional<intptr_t>Debugger::handleDuplicateFunctionNames(
    const std::string_view name, const std::vector<dwarf::die>& functions)  {
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
   for(const auto& die : functions) {
       // auto& die = dieRef.get();
       auto low = dwarf::at_low_pc(die);
       auto lineEntryItr = getLineEntryFromPC(low);
       if(!lineEntryItr) {
           throw std::logic_error("[fatal] In Debugger::setBreakpointAtFunctionName() - "
               "function definition with low pc does not have valid line entry\n");
       }
       auto entry = lineEntryItr.value();
       std::cout << std::dec << "\t[" << count++ << "] " << dwarf::at_name(die)
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

std::optional<intptr_t>Debugger::handleDuplicateFilenames( const std::string_view filepath,
    const std::vector<std::pair<std::string, intptr_t>>& fpAndAddr)  {
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

void Debugger::removeBreakpoint(intptr_t address) {
    if(retAddrFromMain_ && retAddrFromMain_->getAddr() == address && retAddrFromMain_->isEnabled()) {
        std::cerr << "[warning] Attemping to remove breakpoint at the return address of main. "
            "Confirm with [y/n]: ";
        std::string input = "";
        std::getline(std::cin, input);
        if(input.length() > 0 && (input[0] == 'y' || input[0] == 'Y')) {
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
    //std::cerr << "DEBUG: bp removed!\n";

}

void Debugger::removeBreakpoint(std::unordered_map<intptr_t, Breakpoint>::iterator it) {
    if(retAddrFromMain_ && retAddrFromMain_ == &(it->second) && retAddrFromMain_->isEnabled()) {
        std::cerr << "[warning] Attemping to remove breakpoint at the return address of main. "
            "Confirm with [y/n]: ";
        std::string input = "";
        std::getline(std::cin, input);
        if(input.length() > 0 && (input[0] == 'y' || input[0] == 'Y')) {
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

    //std::cerr << "DEBUG: Checking memMap initialize in waitForSignal() \n";

    if(memMap_.initialized() && !exit_) memMap_.reload();

    auto signal = getSignalInfo();
    //std::cerr << "DEBUG: Checking signal info in waitForSignal(): " << std::to_string(signal.si_signo) << "\n";

    switch(signal.si_signo) {
        case SIGTRAP:
            //std::cerr << "DEBUG: handling sigtrap in waitForSignal() \n";

            handleSIGTRAP(signal);
            //std::cerr << "DEBUG: Checking returnAddrFromMain in waitForSignal() \n";

            if(retAddrFromMain_ && getPC() == std::bit_cast<uint64_t>(retAddrFromMain_->getAddr())) {
                cleanup();
                //std::cerr << "DEBUG: cleanup complete! continuing...\n";
                continueExecution();
            }
            //std::cerr << "DEBUG: Returning from waitForSignal() \n";

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
        case TRAP_TRACE:
            //std::cout << "Single-Stepping\n";
            return;
    
        default:
            std::cout << "[warning] Unknown SIGTRAP code: " << signal.si_code << "\n";
    }
}