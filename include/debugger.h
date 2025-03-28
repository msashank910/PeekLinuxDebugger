#pragma once

#include <string>
#include <unordered_map>
#include <sys/types.h>
#include <signal.h>

#include <dwarf/dwarf++.hh>
#include <elf/elf++.hh>

#include "./breakpoint.h"
#include "./memorymap.h"

class Debugger {
    pid_t pid_;
    std::string progName_;
    uint64_t loadAddress_;
    bool exit_;
    uint8_t context_;

    dwarf::dwarf dwarf_;
    elf::elf elf_;
    MemoryMap memMap_;

    std::unordered_map<std::intptr_t, Breakpoint> addrToBp_;

    bool handleCommand(std::string args);   //bool used for spacing
    std::pair<std::unordered_map<intptr_t, Breakpoint>::iterator, bool> 
        setBreakpointAtAddress(std::intptr_t address);
    void removeBreakpoint(std::unordered_map<intptr_t, Breakpoint>::iterator it);
    void removeBreakpoint(std::intptr_t address);
    void dumpBreakpoints() const;
    void continueExecution();

    void singleStep();
    void singleStepBreakpointCheck();
    void stepIn();
    void stepOut();
    void stepOver();
    void stepOverBreakpoint();

    void waitForSignal();
    void handleSIGTRAP(siginfo_t signal);
    siginfo_t getSignalInfo() const;


    uint64_t getPC() const;
    bool setPC(uint64_t val);
    pid_t getPID() const;
    uint8_t getContext() const;
    void setContext(uint8_t context);
    void initializeMemoryMapAndLoadAddress();
    uint64_t offsetLoadAddress(uint64_t addr) const;
    uint64_t addLoadAddress(uint64_t addr) const;
    uint64_t getPCOffsetAddress() const;

    void readMemory(const uint64_t addr, uint64_t &data) const;
    void writeMemory(const uint64_t addr, const uint64_t &data);
    void dumpRegisters() const;

    dwarf::die getFunctionFromPC(uint64_t pc) const;
    dwarf::line_table::iterator getLineEntryFromPC(uint64_t pc) const;
    void printSource(const std::string fileName, const unsigned line, const uint8_t numOfContextLines) const;
    void printSourceAtPC() const;


public:
    Debugger(pid_t pid, std::string progName);
    void run();
    
};