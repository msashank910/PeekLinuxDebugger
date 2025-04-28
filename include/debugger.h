#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <optional>
#include <sys/types.h>
#include <signal.h>

#include <dwarf/dwarf++.hh>
#include <elf/elf++.hh>

#include "./breakpoint.h"
#include "./memorymap.h"
#include "./symbolmap.h"
#include "./state.h"
#include "./config.h"

class Debugger {

    pid_t pid_;
    std::string progName_;
    //bool verbose;
    uint64_t loadAddress_;
    state::Child state_;
    Config globalConfig_;
    Config::DebugConfig* config_;
    //uint8_t context_;
    Breakpoint* retAddrFromMain_;

    dwarf::dwarf dwarf_;
    elf::elf elf_;
    MemoryMap memMap_;
    SymbolMap symMap_;


    std::unordered_map<std::intptr_t, Breakpoint> addrToBp_;
    std::unordered_map<const dwarf::compilation_unit*, std::vector<dwarf::section_offset>> functionDies_;


    void initialize();
    bool handleCommand(const std::string& args, std::string& prevArgs);   //bool used for spacing
    void handleChildState();
    void cleanup();
    
    std::pair<std::unordered_map<intptr_t, Breakpoint>::iterator, bool> 
        setBreakpointAtAddress(std::intptr_t address/*, bool skipLineTableCheck = false*/);
    std::pair<std::unordered_map<intptr_t, Breakpoint>::iterator, bool> 
        setBreakpointAtFunctionName(const std::string_view name);
    std::pair<std::unordered_map<intptr_t, Breakpoint>::iterator, bool> 
        setBreakpointAtSourceLine(const std::string_view file, const unsigned line);   
    void removeBreakpoint(std::unordered_map<intptr_t, Breakpoint>::iterator it);
    void removeBreakpoint(std::intptr_t address);
    void dumpBreakpoints() const;

    void continueExecution();
    void singleStep();
    void singleStepBreakpointCheck();
    bool validMemoryRegionShouldStep(std::optional<dwarf::line_table::iterator> itr, bool shouldStep);
    bool readStackSnapshot(std::vector<std::pair<uint64_t, bool>>& stack, size_t bytes = 64);
    bool writeStackSnapshot(std::vector<std::pair<uint64_t, bool>>& stack, size_t bytes = 64);
    void stepIn();
    void stepOut();
    void stepOver();
    void stepOverBreakpoint();
    void skipUnsafeInstruction(const size_t bytes = 8);
    void jumpToInstruction(const uint64_t newRip);
    void printBacktrace();

    void waitForSignal();
    void handleSIGTRAP(siginfo_t signal);
    siginfo_t getSignalInfo() const;


    uint64_t getPC() const;
    bool setPC(uint64_t val);
    pid_t getPID() const;
    uint8_t getContext() const;
    void setContext(uint8_t context);
    void initializeMapsAndLoadAddress();
    uint64_t offsetLoadAddress(uint64_t addr) const;
    uint64_t addLoadAddress(uint64_t addr) const;
    uint64_t getPCOffsetAddress() const;
    

    void readMemory(const uint64_t addr, uint64_t &data) const;
    void writeMemory(const uint64_t addr, const uint64_t &data);
    void dumpRegisters() const;

    void initializeFunctionDies();
    void dumpFunctionDies();

    std::optional<intptr_t> handleDuplicateFunctionNames(const std::string_view, 
        const std::vector<dwarf::die>& functions);
    std::optional<intptr_t> handleDuplicateFilenames(const std::string_view filepath, 
        const std::vector<std::pair<std::string, intptr_t>>& fpAndAddr);

    std::optional<dwarf::die> getFunctionFromPCOffset(uint64_t pc) const;
    std::optional<dwarf::line_table::iterator> getLineEntryFromPC(uint64_t pc) const;

    void readVariable(const std::string& var) const;
    void writeVariable(const std::string& var, const std::string& val);
    void getVariables(const uint64_t pc) const;
    void dumpVariables() const;

    void printSource(const std::string fileName, const unsigned line, const uint8_t numOfContextLines) const;
    void printSourceAtPC(); //can terminate debugger
    void printMemoryLocationAtPC() const;


public:
    Debugger(pid_t pid, std::string progName);
    void run();
    
};