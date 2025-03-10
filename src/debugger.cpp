#include "../include/debugger.h"
#include "../include/util.h"
#include "../include/register.h"


#include <linenoise.h>
#include <iostream>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <cstring>


using namespace util;
using namespace reg;

//Debugger Methods
Debugger::Debugger(pid_t pid, std::string progName) : pid_(pid), progName_(progName) {}

void Debugger::run() {
    int waitStatus;
    auto options = 0;
    waitpid(pid_, &waitStatus, options);        //may simplify later

    char* line;

    while((line = linenoise("[__pld__] ")) != nullptr) {
        handleCommand(line);
        linenoiseHistoryAdd(line);  //may need to initialize history
        linenoiseFree(line);
    }
}

pid_t Debugger::getPID() {return pid_;}

uint64_t Debugger::getPC() {
    return getRegisterValue(pid_, Reg::rip);
}

bool Debugger::setPC(uint64_t val) {
    return setRegisterValue(pid_, Reg::rip, val);
}


void Debugger::handleCommand(std::string args) {
    auto argv = splitLine(args, ' ');

    if(isPrefix(argv[0], "continue")) {
        std::cout << "Continue Execution\n";
    }
    else if(isPrefix(argv[0], "break")) {
        //std::cout << "Placing breakpoint\n";
        if(argv.size() > 1)     //may change to stoull in future
            setBreakpointAtAddress(std::stol(strip0x(argv[1]), nullptr, 16)); 
        else
            std::cout << "Please specify address!\n";
    }
    else if(isPrefix(argv[0], "pid")) {
        std::cout << "Retrieving child process ID...\n";
        std::cout << std::dec << "pID = " << getPID() << "\n";

    }
    else if(isPrefix(argv[0], "register_read") || argv[0] == "rr") {
        if(argv.size() > 1)  {
            Reg r = getRegFromName(argv[1]);

            if(r != Reg::INVALID_REG) {
                std::cout << argv[1] << ": 0x" << getRegisterValue(pid_, r) << "\n";
            }
            else {
                std::cout << "Incorrect Register Name! (ex: r15)\n";
            }
        } 
        else
            std::cout << "Please specify register!\n";
    }
    else if(isPrefix(argv[0], "register_write") || argv[0] == "rw") {
        if(argv.size() > 2)  {
            Reg r = getRegFromName(argv[1]);
            std::string data = strip0x(argv[2]);

            if(r != Reg::INVALID_REG && setRegisterValue(pid_, r, std::stol(data, nullptr, 16))) {
                std::cout << argv[1] << ": 0x" << getRegisterValue(pid_, r) << " --> 0x" << data << "\n";
            }
            else {
                std::cout << "Incorrect register name or data value! (ex: r15 0xFFFFFFFFFFFFFFFF)\n";
            }
        } 
        else
            std::cout << "Please specify register and data!\n";
    }
    else if(isPrefix(argv[0], "dump")) {
        std::cout << "Dumping registers...\n";
        dumpRegisters();
    }
    else if(isPrefix(argv[0], "read_memory")) {
        if(argv.size() > 1)  {
            uint64_t data;
            std::string addr = strip0x(argv[1]);

            if(readMemory(std::stol(addr, nullptr, 16), data)) {
                std::cout << "Memory at 0x" << addr << ": " << data << "\n";
            }
        } 
        else
            std::cout << "Please specify address!\n";
    }
    else if(isPrefix(argv[0], "write_memory")) {
        if(argv.size() > 1)  {
            uint64_t data;
            std::string addr = strip0x(argv[1]);

            writeMemory(std::stol(addr, nullptr, 16), data);

            if(readMemory(std::stol(addr, nullptr, 16), data)) {
                std::cout << "Memory at 0x" << addr << ": " << data << "\n";
            }
        } 
        else
            std::cout << "Please specify address!\n";
    }
    else if(argv[0] == "pc") {
        std::cout << "Retrieving program counter...\n" << "Program Counter (rip): " << getPC() << "\n";
    }
    else {
        std::cout << "Invalid Command!\n";
    }
}

void Debugger::setBreakpointAtAddress(std::intptr_t address) {    
    std::cout << "Setting Breakpoint at: 0x" << std::hex << address << "\n";    //changed to hex
    
    auto [it, inserted] = addrToBp_.emplace(address, Breakpoint(pid_, address));
    if(inserted) {
        it->second.enable();
        std::cout << "Breakpoint successfully set!\n";
    }
    else {
        std::cout << "Breakpoint already exists!\n";  
    }

}

void Debugger::continueExecution() {
    ptrace(PTRACE_CONT, pid_, nullptr, nullptr);

    int waitStatus;
    auto options = 0;
    waitpid(pid_, &waitStatus, options);
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
        std::cout << rd.regName << ": " << std::hex << std::uppercase << "0x" << *regVals << "\n";    
        ++regVals;
    }
}

//add support for multiple words eventually (check notes/TODO)
bool Debugger::readMemory(const uint64_t &addr, uint64_t &data) {  //PEEKDATA, show errors if needed
    errno = 0;
    long res = ptrace(PTRACE_PEEKDATA, pid_, &addr, &data);

    if(errno && res == -1) {
        std::cerr << "ptrace error: " << strerror(errno) << ".\n Check Memory Address!\n";
        return false;
    }
    return true;
}

bool Debugger::writeMemory(const uint64_t &addr, uint64_t &data) {
    errno = 0;
    long res = ptrace(PTRACE_POKEDATA, pid_, &addr, &data);

    if(errno && res == -1) {
        std::cerr << "ptrace error: " << strerror(errno) << ".\n Check Memory Address!\n";
        return false;
    }
    return true;
}