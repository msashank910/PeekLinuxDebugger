#pragma once

#include <string>
#include <unordered_map>
#include <sys/types.h>


#include "./breakpoint.h"

class Debugger {
    pid_t pid_;
    std::string progName_;
    std::unordered_map<std::intptr_t, Breakpoint> addrToBp_;

    void handleCommand(std::string args);
    void continueExecution();
    void setBreakpointAtAddress(std::intptr_t address);
    bool readMemory(const uint64_t &addr, uint64_t &data);
    bool writeMemory(const uint64_t &addr, uint64_t &data);

    void dumpRegisters();

public:
    Debugger(pid_t pid, std::string progName);
    pid_t getPID();
    void run();
    
    uint64_t getPC();
    bool setPC(uint64_t val);
};