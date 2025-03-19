#include "../include/debugger.h"
#include "../include/util.h"
#include "../include/register.h"


#include <linenoise.h>
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
#include <string_view>
#include <bit>
#include <memory>
#include <stdexcept>
#include <algorithm>


using namespace util;
using namespace reg;

//Debugger Methods
Debugger::Debugger(pid_t pid, std::string progName) : pid_(pid), progName_(std::move(progName)), exit_(false) {
    auto fd = open(progName_.c_str(), O_RDONLY);
    
    elf_ = elf::elf(elf::create_mmap_loader(fd));
    dwarf_ = dwarf::dwarf(dwarf::elf::create_loader(elf_));

    context_ = 4;

}

void Debugger::run() {
    waitForSignal();
    initializeLoadAddress();

    char* line;

    while(!exit_ && (line = linenoise("[__pld__] ")) != nullptr) {
        if(handleCommand(line)) {std::cout << std::endl;}
        linenoiseHistoryAdd(line);  //may need to initialize history
        linenoiseFree(line);
    }
}

void Debugger::initializeLoadAddress() {
    if(elf_.get_hdr().type == elf::et::dyn) {
        
        std::unique_ptr<char, decltype(&free)> ptr(realpath(progName_.c_str(), nullptr), free);
        if(!ptr) {
            throw std::logic_error("Invalid or nonexistant program name, realpath() failed!");
        }

        std::string absFilePath = ptr.get();

        std::ifstream file;
        file.open("/proc/" + std::to_string(getPID()) + "/maps");
        std::string addr = "";
        std::string possibleFilePath = "";

        std::getline(file, addr, '-');
        std::getline(file, possibleFilePath, '\n');
        possibleFilePath.erase(0, possibleFilePath.find_last_of(" \t") + 1);

        while(possibleFilePath != absFilePath && std::getline(file, addr, '-')) {
            std::getline(file, possibleFilePath, '\n');
            possibleFilePath.erase(0, possibleFilePath.find_last_of(" \t") + 1);
        } 
        
        loadAddress_ = std::stol(addr, nullptr, 16);

    }
    else
        loadAddress_ = 0;

    //std::cout << "DEBUGGING MESSAGE: loadAddress_ = " << std::hex << std::uppercase 
    //    << loadAddress_ << std::endl;
}

uint64_t Debugger::offsetLoadAddress(uint64_t addr) { return addr - loadAddress_;}

uint64_t Debugger::getPCOffsetAddress() {return offsetLoadAddress(getPC());}

pid_t Debugger::getPID() {return pid_;}

uint64_t Debugger::getPC() { return getRegisterValue(pid_, Reg::rip); }

bool Debugger::setPC(uint64_t val) { return setRegisterValue(pid_, Reg::rip, val); }

unsigned Debugger::getContext() {return context_;}

void Debugger::setContext(unsigned context) {context_ = context;}



bool Debugger::handleCommand(std::string args) {
    if(args.empty()){
        return false;
    }
    auto argv = splitLine(args, ' ');

    if(isPrefix(argv[0], "continue_execution")) {
        std::cout << "Continue Execution..." << std::endl;
        continueExecution();
        //return false;
    }
    else if(isPrefix(argv[0], "breakpoint")) {
        //std::cout << "Placing breakpoint\n";
        if(argv.size() > 1)   {  //may change to stoull in future 
            uint64_t num;
            auto stripped0xAddr = strip0x(argv[1]);
            
            if(!stripped0xAddr.empty() && stripped0xAddr[0] == '*' 
                && validStol(num, stripped0xAddr.substr(1))
                && num < UINT64_MAX - loadAddress_) {
    
                num += loadAddress_;
                setBreakpointAtAddress(std::bit_cast<intptr_t>(num));

            }
            else if(validStol(num, stripped0xAddr) && num < UINT64_MAX - loadAddress_) {
                setBreakpointAtAddress(std::bit_cast<intptr_t>(num));

            }
            else
                std::cout << "Invalid address!\n"
                    << "Pass a valid relative address (*0x1234) or a valid absolute address (0xFFFFFFFF).";
        }
        else
            std::cout << "Please specify address!";
    }
    // else if(isPrefix(argv[0], "single_step") || argv[0] == "ss") {
    //     std::cout << "Single Stepping..." << std::endl;
    //     singleStep(); //returns bool if wanting to check
    // }
    else if(isPrefix(argv[0], "pid")) {
        std::cout << "Retrieving child process ID...\n";
        std::cout << std::dec << "pID = " << getPID();

    }
    else if(argv[0] == "register_read" || argv[0] == "rr") {
        if(argv.size() > 1)  {
            Reg r = getRegFromName(argv[1]);

            if(r != Reg::INVALID_REG) {
                std::cout << std::hex << std::uppercase
                    << argv[1] << ": 0x" << getRegisterValue(pid_, r);
            }
            else {
                std::cout << "Incorrect Register Name! (ex: r15)";
            }
        } 
        else
            std::cout << "Please specify register!";
    }
    else if(argv[0] == "register_write" || argv[0] == "rw") {
        if(argv.size() > 2)  {
            Reg r = getRegFromName(argv[1]);
            auto stripped0xData = strip0x(argv[2]);

            if(r == Reg::INVALID_REG) {
                std::cout << "Incorrect register name!";
                return true;
            }

            auto oldVal = getRegisterValue(pid_, r);
            uint64_t data; 
            if(!validStol(data, stripped0xData)) {
                std::cout << "Please specify valid data!";
            }
            else {
                setRegisterValue(pid_, r, data); 
                std::cout << std::hex << std::uppercase 
                << argv[1] << ": 0x" << oldVal << " --> 0x" << getRegisterValue(pid_, r);
            }
        } 
        else
            std::cout << "Please specify register and data!";
    }
    else if(argv[0] == "dump_registers" || argv[0] == "dr") {
        std::cout << "Dumping registers...";
        dumpRegisters();
    }
    else if(argv[0] == "dump_breakpoints" || argv[0] == "db") {
        std::cout << "Dumping breakpoints...\n";
        dumpBreakpoints();
    }
    else if(argv[0] == "read_memory" || argv[0] == "rm") {
        if(argv.size() > 1)  {
            uint64_t addr;
            auto stripped0xaddr = strip0x(argv[1]);

            if(!validStol(addr, stripped0xaddr)) {
                std::cout << "Please specify valid address!";
            }
            else {
                uint64_t data;
                readMemory(addr, data);
                std::cout << std::hex << std::uppercase 
                << "Read to memory at 0x" << addr << "--> " << data;
            }
            
        } 
        else
            std::cout << "Please specify address!";
    }
    else if(argv[0] == "write_memory" || argv[0] == "wm") {
        if(argv.size() > 1)  {
            uint64_t addr;
            auto stripped0xaddr = strip0x(argv[1]);

            if(!validStol(addr, stripped0xaddr)) {
                std::cout << "Please specify valid address!";
            }
            else {
                uint64_t data;
                writeMemory(addr, data);
                std::cout << std::hex << std::uppercase << "Written to memory at 0x" << addr 
                << "--> " << data;
            }
            
        } 
        else
            std::cout << "Please specify address!";
    }
    else if(argv[0] == "program_counter" || argv[0] == "pc") {
        std::cout << std::hex << std::uppercase << "Retrieving program counter...\n" << "Program Counter (rip): " 
            << getPC();
    }
    else if(argv[0] == "help") {
        std::cout << "Welcome to Peek!";
    }
    else if(argv[0] == "q" || argv[0] == "e" || argv[0] == "exit" || argv[0] ==  "quit") {
        std::cout << "Exiting....";
        exit_ = true;
    }
    else {
        std::cout << "Invalid Command!";
    }

    return true;        //Command processed/spacing needed

}

void Debugger::setBreakpointAtAddress(std::intptr_t address) {
    std::cout << "Setting Breakpoint at: 0x" << std::hex << address << "\n";    //changed to hex
    
    auto [it, inserted] = addrToBp_.emplace(address, Breakpoint(pid_, address));
    if(inserted) {
        it->second.enable();
        std::cout << "Breakpoint successfully set!";
    }
    else {
        std::cout << "Breakpoint already exists!";  
    }

}


void Debugger::dumpRegisters() {
    /*  Issues:
        - When dumping, registers with 0 in bytes above lsb are omitted
        - dumps in a line, kinda looks ugly
        - maybe format into a neat table
    */
    user_regs_struct rawRegVals;
    auto regVals = getAllRegisterValues(pid_, rawRegVals);
    for(auto& rd : regDescriptorList) {     //uppercase vs nouppercase (default)
        std::cout << "\n" << rd.regName << ": " << std::hex << std::uppercase << "0x" << *regVals;    
        ++regVals;
    }
}

void Debugger::dumpBreakpoints() {
    if(addrToBp_.empty()) {
        std::cout << "No breakpoints set!";
        return;
    }
    int count = 1;
    for(auto& it : addrToBp_) {
        std::cout << "\n" << count << ") 0x" << it.first << " (" 
            << ((it.second.isEnabled()) ? "enabled" : "disabled") << ")";
    }
}

//add support for multiple words eventually (check notes/TODO)
void Debugger::readMemory(const uint64_t addr, uint64_t &data) {  //PEEKDATA, show errors if needed
    errno = 0;
    long res = ptrace(PTRACE_PEEKDATA, pid_, addr, nullptr);
    data = std::bit_cast<uint64_t>(res);
    
    if(errno && res == -1) {
        throw std::runtime_error("ptrace error: " + std::string(strerror(errno)) + 
            ".\n Check Memory Address!\n");
    }
}

void Debugger::writeMemory(const uint64_t addr, const uint64_t &data) {
    errno = 0;
    long res = ptrace(PTRACE_POKEDATA, pid_, addr, data);

    if(errno && res == -1) {
        throw std::runtime_error("ptrace error: " + std::string(strerror(errno)) + 
            ".\n Check Memory Address!\n");
    }
}

void Debugger::continueExecution() {
    stepOverBreakpoint();
    ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
    waitForSignal();

}

void Debugger::singleStep() {
    errno = 0;
    //std::cout << "SingleStepping!\n";
    long res = ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr);
    
    if(errno && res == -1) {
        throw std::runtime_error("ptrace error: " + std::string(strerror(errno)) + 
            "..\n Single Step Failed!\n");
    }
    waitForSignal();
    //std::cout << "Finished SingleStepping!\n";

    //return true;
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
    //return ret;
}

dwarf::die Debugger::getFunctionFromPC(uint64_t pc) {
    for(const auto& cu : dwarf_.compilation_units()) {
        if(dwarf::die_pc_range(cu.root()).contains(pc)) {
            for(auto die : cu.root()) {
                if(die.tag == dwarf::DW_TAG::subprogram && dwarf::die_pc_range(die).contains(pc)) {
                    return die;
                }
            }
        }
    }
    throw std::out_of_range("Function not found for pc. Something is definitely wrong!\n");
}

dwarf::line_table::iterator Debugger::getLineEntryFromPC(uint64_t pc) {
    for(const auto& cu : dwarf_.compilation_units()) {
        if(dwarf::die_pc_range(cu.root()).contains(pc)) {
            const auto lineTable = cu.get_line_table();
            const auto lineEntryItr = lineTable.find_address(pc);
            if(lineEntryItr != lineTable.end()) return lineEntryItr;
            else throw std::out_of_range("Line Entry not found in table!\n");
        }
    }
    throw std::out_of_range("PC not found in any line table!\n");
}

void Debugger::printSource(const std::string fileName, unsigned line, unsigned numOfContextLines) {
    unsigned start = (line > numOfContextLines) ? line - numOfContextLines : 1;
    unsigned end = line + numOfContextLines + 1 + ((line - start >= numOfContextLines) ? (0) :
        (numOfContextLines - start - line));    //could optimize formula
    
    std::fstream file;
    file.open(fileName, std::ios::in);
    
    std::string buffer = "";
    
    unsigned index = 0;

    for(; index < start; index++) {
        std::getline(file, buffer, '\n');
    }
    while(getline(file, buffer, '\n') && index < end) {
        std::cout << std::dec << (index == line ? "> " : " ") << index << " " << buffer << "\n";
        ++index;
    }
    std::cout << std::endl; //flush buffer and extra newline just in case
}

void Debugger::waitForSignal() {
    int options = 0;
    int wait_status;
    errno = 0;
    //std::cout << "Getting ready to wait!!\n";

    if(waitpid(pid_, &wait_status, options) == -1 && errno) {
        std::cerr << "ptrace error: " << strerror(errno) << ".\n WaitPid failed!\n";
        throw std::runtime_error("ptrace error:" + std::string(strerror(errno)) +
            ".\n Waitpid failed!\n");
    }

    if(WIFEXITED(wait_status)) {
        std::cout << "\n**Child process has complete normally. Thank you for using Peek!**" << std::endl;
        exit_ = true;
        return;
    }
    else if(WIFSIGNALED(wait_status)) {
        std::cout << "\n**Warning! Child process has terminated abnormally! (Thank you for using Peek.)**" << std::endl;
        exit_ = true;
        return;
    }

    auto signal = getSignalInfo();
    switch(signal.si_signo) {
        case SIGTRAP:
            handleSIGTRAP(signal);
            return;
        case SIGSEGV:
            std::cerr << "Segmentation Error: " << signal.si_signo << ", Reason: " << signal.si_code << "\n";
            break;
        default:
            std::cerr << "Signal: " << signal.si_signo << ", Reason: " << signal.si_code << "\n";
            break;
    }

    auto offset = getPCOffsetAddress();
    auto lineEntryItr = getLineEntryFromPC(offset);
    printSource(lineEntryItr->file->path, lineEntryItr->line, context_); 
}

siginfo_t Debugger::getSignalInfo() {
    errno = 0;
    siginfo_t data;
    if(ptrace(PTRACE_GETSIGINFO, pid_, nullptr, &data) == -1 && errno) {
        throw std::runtime_error("Ptrace get signal info has failed! " + std::string(strerror(errno)) 
            + ".\n");
    }
    return data;
}

void Debugger::handleSIGTRAP(siginfo_t signal) {
    switch(signal.si_code) {
        case SI_KERNEL:
        case TRAP_BRKPT: {
            setPC(getPC() - 1); //pc is being decremented for stepOverBreakpoint()
            std::cout << "Hit breakpoint at: " << std::hex << std::uppercase << getPC() << "\n";
            auto offset = getPCOffsetAddress();
            auto lineEntryItr = getLineEntryFromPC(offset);
            printSource(lineEntryItr->file->path, lineEntryItr->line, context_);
            return;
        }
        case 0:
            return;
        case TRAP_TRACE:
            std::cout << "Single-Stepping\n";
            return;
    
        default:
            std::cout << "Unknown SIGTRAP code: " << signal.si_code << "\n";
    }

}