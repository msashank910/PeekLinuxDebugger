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
Debugger::Debugger(pid_t pid, std::string progName) : pid_(pid), progName_(progName), exit_(false) {}

void Debugger::run() {
    // int waitStatus;
    // auto options = 0;
    // waitpid(pid_, &waitStatus, options);        //may simplify later

    waitForSignal();

    char* line;

    while(!exit_ && (line = linenoise("[__pld__] ")) != nullptr) {
        if(handleCommand(line)) {std::cout << std::endl;}
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


bool Debugger::handleCommand(std::string args) {
    if(args.empty()){
        return false;
    }
    auto argv = splitLine(args, ' ');

    if(isPrefix(argv[0], "continue_execution")) {
        std::cout << "Continue Execution..." << std::endl;
        continueExecution();
    }
    else if(isPrefix(argv[0], "breakpoint")) {
        //std::cout << "Placing breakpoint\n";
        if(argv.size() > 1)     //may change to stoull in future
            setBreakpointAtAddress(std::stol(strip0x(argv[1]), nullptr, 16)); 
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
            std::string data = strip0x(argv[2]);

            if(r != Reg::INVALID_REG && setRegisterValue(pid_, r, std::stol(data, nullptr, 16))) {
                std::cout << std::hex << std::uppercase 
                    << argv[1] << ": 0x" << getRegisterValue(pid_, r) << " --> 0x" << data;
            }
            else {
                std::cout << "Incorrect register name or data value! (ex: r15 0xFFFFFFFFFFFFFFFF)";
            }
        } 
        else
            std::cout << "Please specify register and data!";
    }
    else if(isPrefix(argv[0], "dump_registers") || argv[0] == "dr") {
        std::cout << "Dumping registers...";
        dumpRegisters();
    }
    else if(argv[0] == "read_memory" || argv[0] == "rm") {
        if(argv.size() > 1)  {
            uint64_t data;
            std::string addr = strip0x(argv[1]);

            if(readMemory(std::stol(addr, nullptr, 16), data)) {
                std::cout << std::hex << std::uppercase 
                    << "Memory at 0x" << addr << ": " << data;
            }
        } 
        else
            std::cout << "Please specify address!";
    }
    else if(argv[0] == "write_memory" || argv[0] == "wm") {
        if(argv.size() > 1)  {
            uint64_t data;
            std::string addr = strip0x(argv[1]);

            if(writeMemory(std::stol(addr, nullptr, 16), data)) {
                std::cout << std::hex << std::uppercase << "Memory at 0x" << addr 
                    << ": " << data;
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
    else {
        std::cout << "Invalid Command!";
    }

    return true;        //Command processed

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

void Debugger::continueExecution() {
    if(stepOverBreakpoint()) {
        //std::cout << "Stepped over breakpoint!\n";
        ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
        //std::cout << "Continuing ptrace is ok!\n";

        waitForSignal();
        //std::cout << "waited for signal!\n";

    }
    else {
        std::cout << "\nStep over Breakpoint failed! Cannot Continue Execution.";
    }
}

bool Debugger::singleStep() {
    errno = 0;
    //std::cout << "SingleStepping!\n";
    long res = ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr);
    
    if(errno && res == -1) {
        std::cerr << "ptrace error: " << strerror(errno) << ".\n Single Step Failed!\n";
        return false;
    }
    waitForSignal();
    //std::cout << "Finished SingleStepping!\n";

    return true;
}
bool Debugger::stepOverBreakpoint() {
    uint64_t addr = getPC() - 1;
    if(setPC(addr)) {
        auto it = addrToBp_.find(static_cast<intptr_t>(addr));
        
        if(it != addrToBp_.end()) {
            if(it->second.isEnabled()){
                it->second.disable();
            }
            if(singleStep()){
                it->second.enable();
            }
        }
        return true;
    }
    std::cerr << "SetPC() failed!\n";
    return false;
}

void Debugger::waitForSignal() {
    int options = 0;
    int wait_status;
    errno = 0;
    //std::cout << "Getting ready to wait!!\n";

    if(waitpid(pid_, &wait_status, options) == -1 && errno) {
        std::cerr << "ptrace error: " << strerror(errno) << ".\n WaitPid failed!\n";
    }

    if(WIFEXITED(wait_status)) {
        std::cout << "\n**Child process has complete normally. Thank you for using Peek!**" << std::endl;
        exit_ = true;
    }
    else if(WIFSIGNALED(wait_status)) {
        std::cout << "\n**Warning! Child process has terminated abnormally! (Thank you for using Peek.)**" << std::endl;
        exit_ = true;
    }
}