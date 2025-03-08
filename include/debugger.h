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
    
    void dumpRegisters();

public:
    Debugger(pid_t pid, std::string progName);
    int getPID();
    void run();
};