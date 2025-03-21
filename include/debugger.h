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

    bool handleCommand(std::string args);   //bool used for spacing
    void setBreakpointAtAddress(std::intptr_t address);
    void dumpBreakpoints() const;
    void continueExecution();
    void singleStep();
    void stepOverBreakpoint();
    void waitForSignal();
    void handleSIGTRAP(siginfo_t signal);
    siginfo_t getSignalInfo() const;


    uint64_t getPC() const;
    bool setPC(uint64_t val);
    pid_t getPID() const;
    unsigned getContext() const;
    void setContext(unsigned context);
    void initializeLoadAddress();
    uint64_t offsetLoadAddress(uint64_t addr) const;
    uint64_t addLoadAddress(uint64_t addr) const;
    uint64_t getPCOffsetAddress() const;

    void readMemory(const uint64_t addr, uint64_t &data) const;
    void writeMemory(const uint64_t addr, const uint64_t &data);
    void dumpRegisters() const;

    dwarf::die getFunctionFromPC(uint64_t pc) const;
    dwarf::line_table::iterator getLineEntryFromPC(uint64_t pc) const;
    void printSource(const std::string fileName, const unsigned line, const unsigned numOfContextLines) const;


public:
    Debugger(pid_t pid, std::string progName);
    void run();
    
};