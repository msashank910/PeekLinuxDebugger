#pragma once

#include <string>
#include <unordered_map>
#include <sys/types.h>
#include <signal.h>

#include <dwarf/dwarf++.hh>
#include <elf/elf++.hh>

#include "./breakpoint.h"

class Debugger {
    pid_t pid_;
    std::string progName_;
    uint64_t loadAddress_;
    unsigned context_;

    dwarf::dwarf dwarf_;
    elf::elf elf_;

    bool exit_;
    std::unordered_map<std::intptr_t, Breakpoint> addrToBp_;

    bool handleCommand(std::string args);   //returns if command was processed
    void setBreakpointAtAddress(std::intptr_t address);
    void dumpBreakpoints();
    void continueExecution();
    void singleStep();
    void stepOverBreakpoint();
    void waitForSignal();
    void handleSIGTRAP(siginfo_t signal);
    siginfo_t getSignalInfo();


    uint64_t getPC();
    bool setPC(uint64_t val);
    pid_t getPID();
    unsigned getContext();
    void setContext(unsigned context);
    void initializeLoadAddress();
    uint64_t offsetLoadAddress(uint64_t addr);
    uint64_t getPCOffsetAddress();

    void readMemory(const uint64_t &addr, uint64_t &data);
    void writeMemory(const uint64_t &addr, uint64_t &data);
    void dumpRegisters();

    dwarf::die getFunctionFromPC(uint64_t pc);
    dwarf::line_table::iterator getLineEntryFromPC(uint64_t pc);
    void printSource(const std::string fileName, const unsigned line, const unsigned numOfContextLines);


public:
    Debugger(pid_t pid, std::string progName);
    void run();
    
};