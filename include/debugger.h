#pragma once

#include <string>
#include <unordered_map>
#include <sys/types.h>


#include "./breakpoint.h"

class Debugger {
    pid_t pid_;
    std::string progName_;
    bool exit_;
    std::unordered_map<std::intptr_t, Breakpoint> addrToBp_;

    bool handleCommand(std::string args);   //returns if command was processed
    
    void setBreakpointAtAddress(std::intptr_t address);
    void dumpBreakpoints();
    void continueExecution();
    bool singleStep();
    bool stepOverBreakpoint();
    void waitForSignal();

    uint64_t getPC();
    bool setPC(uint64_t val);
    pid_t getPID();

    bool readMemory(const uint64_t &addr, uint64_t &data);
    bool writeMemory(const uint64_t &addr, uint64_t &data);
    void dumpRegisters();

public:
    Debugger(pid_t pid, std::string progName);
    void run();
    
};