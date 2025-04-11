#include "../include/debugger.h"
#include "../include/memorymap.h"
#include "../include/util.h"
#include "../include/register.h"
#include "../include/breakpoint.h"

#include <linenoise.h>
#include <dwarf/dwarf++.hh>
#include <elf/elf++.hh>

#include <iostream>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <bit>
#include <algorithm>
//#include 


//using namespaces util and reg
using namespace util;
using namespace reg;

//Debugger function: run()
void Debugger::run() {
    //std::cerr << "DEBUG: Calling waitForSignal() in run() \n";

    waitForSignal();                       //debugger has control after ptrace TRACE_ME
    //std::cerr << "DEBUG: Calling initialize() in run() \n";

    initialize();

    char* line;
    std::string prevArgs = "";
    while(state_ == Child::running && (line = linenoise("[__p|d__] ")) != nullptr) {
        if(handleCommand(line, prevArgs)) {std::cout << std::endl;} //bool return for spacing/flushing
        linenoiseHistoryAdd(line);  //may need to initialize history
        linenoiseFree(line);
    }

    bool killProcess = false;
    if(state_ == Child::detached) {
        std::cout << "End child process? [y/n] ";
        std::string response = "";
        std::getline(std::cin, response);
        if(killProcess = (response.length() > 0 && (response[0] == 'y' || response[0] == 'Y'))) {
            std::cerr << "[debug] Ending child process. Thank you for using Peek!\n";
        }
    }

    if(state_ != Child::detached || killProcess) {
        kill(pid_, SIGTRAP);
        return;
    }
    cleanup();
    std::cerr << "[debug] Cleanup complete! continuing...\n";
    continueExecution();
}



//Debugger function: handleCommand()
bool Debugger::handleCommand(const std::string& args, std::string& prevArgs) {
    auto input = (args.empty() ? prevArgs : args);
    if(input.empty()) return false;
    std::vector<std::string> argv;
    argv = splitLine(input, ' ');
    if(argv.empty()) return false;
    prevArgs = input;

    if(isPrefix(argv[0], "continue_execution")) {
        std::cout << "[debug] Continue Execution..." << std::endl;
        continueExecution();
        printSourceAtPC();
        printMemoryLocationAtPC();
        //return false;
    }
    else if(argv[0] == "debug") {   //debug command args currently - "debug" <name> ["strict"]
        if(argv.size() > 1 && argv[1].length() > 0 && !hasWhiteSpace(argv[1])) {
            bool strict = argv.size() > 2 && argv[2] == "strict";
            auto list = symMap_.getSymbolListFromName(argv[1], strict);

            std::cout << "[debug] Symbols with name '" << argv[1] 
                << "':\n--------------------------------------------------------\n";
            SymbolMap::dumpSymbolList(list, argv[1], strict);
            std::cout << "--------------------------------------------------------"; 
        }
        else std::cout << "[error] Symbol name is invalid!";
    }
    else if(isPrefix(argv[0], "breakpoint")) {
        //std::cout << "Placing breakpoint\n";
        if(argv.size() < 2 || argv[1].length() < 1)
            std::cout << "[error] Please specify address, source line, or function name!";
        //reordered because source files may begin with a number
        else if(argv[1].find(':') != std::string::npos) {
            std::string_view file = argv[1]; 
            file = file.substr(0, file.find_last_of(':')); 

            if(file.length() == argv[1].length() - 1) {
                std::cout << "[error] Specify line number at source file!";
                return true;
            }
            std::string_view line = argv[1].substr(file.length() + 1);
            uint64_t num;
            if(!validDecStol(num, line) || num > UINT32_MAX) {
                std::cout << "[error] Specify valid line number after ':'.";
                return true;
            }
            auto[it, inserted] = setBreakpointAtSourceLine(file, num);

            if(it == addrToBp_.end()) {
                std::cout << "[error] Could not resolve filepath or line number!";
                return true;
            }
            else if(!inserted) {
                std::cout << "[error] Breakpoint already exists!";
                return true;
            }
            auto chunk = memMap_.getChunkFromAddr(std::bit_cast<uint64_t>(it->first));
            auto memSpace = chunk ? MemoryMap::getFileNameFromChunk(chunk.value()) : "Unmapped Memory";
            std::cout << "[debug] Setting Breakpoint at: " << argv[1] << " (0x" << std::hex << it->first
                    << ") --> " << memSpace  << "\n";
        }
        else if(argv[1][0] == '*' || ::isdigit(argv[1][0]))   {  //may change to stoull in future 
            uint64_t addr;
            bool relativeAddr = (!argv[1].empty() && argv[1][0] == '*');
            auto stringViewAddr = stripAddrPrefix(argv[1]);
            
            if(validHexStol(addr, stringViewAddr) && addr < UINT64_MAX - loadAddress_) {
                //std::cout << "passed validHexStol in breakpoint\n";
                if(relativeAddr) {
                    //std::cout << "load address added\n";
                    addr = addLoadAddress(addr);
                }
                auto [it, inserted] = setBreakpointAtAddress(std::bit_cast<intptr_t>(addr));
                if(it == addrToBp_.end())
                    return true;
                else if(!inserted) {
                    std::cout << "[error] Breakpoint already exists!";
                    return true;
                }

                auto chunk = memMap_.getChunkFromAddr(addr);
                auto memSpace = chunk ? MemoryMap::getFileNameFromChunk(chunk.value()) : "Unmapped Memory";
                std::cout << "[debug] Setting Breakpoint at: 0x" << std::hex << addr
                    << (relativeAddr ? " (0x" + std::string(stringViewAddr) + ") " : " ")
                    << "--> " << memSpace  << "\n";
            }
            else
                std::cout << "[error] Invalid address!\n[info] Pass a valid relative address (*0x1234) "
                    << "or a valid absolute address (0xFFFFFFFF).";
        }
        else {
            std::string_view func = argv[1];
            auto[it, inserted] = setBreakpointAtFunctionName(func);
            if(it == addrToBp_.end()) {
                std::cout << "[error] Could not resolve function name!";
                return true;
            }
            else if(!inserted) {
                std::cout << "[error] Breakpoint already exists!";
                return true;
            }
            auto chunk = memMap_.getChunkFromAddr(std::bit_cast<uint64_t>(it->first));
            auto memSpace = chunk ? MemoryMap::getFileNameFromChunk(chunk.value()) : "Unmapped Memory";
            std::cout << "[debug] Setting Breakpoint at: " << func << " (" << std::hex << it->first
                    << ") --> " << memSpace  << "\n";
        }

    }
    else if(argv[0] == "breakpoint_enable" || argv[0] == "be") {
        //std::cout << "Enable breakpoints...\n";
        if(argv.size() > 1)   {  //may change to stoull in future 
            uint64_t addr;
            bool relativeAddr = (!argv[1].empty() && argv[1][0] == '*');
            auto stringViewAddr = stripAddrPrefix(argv[1]);
            
            if(validHexStol(addr, stringViewAddr) && addr < UINT64_MAX - loadAddress_) {
                if(relativeAddr) {
                    addr = addLoadAddress(addr);
                }
                auto it = addrToBp_.find(std::bit_cast<intptr_t>(addr));
                
                if(it != addrToBp_.end()) {
                    if(!it->second.isEnabled()) it->second.enable();

                    std::cout << "[debug] Breakpoint at 0x" << std::hex << it->first 
                        << " (0x" << offsetLoadAddress(addr) << ") is enabled!";
                }
                else {
                    std::cout << "[error] No breakpoint found at address: 0x" << addr;
                    return true;
                }
            }
            else
                std::cout << "[error] Invalid address!\n[info] Pass a valid relative address (*0x1234) "
                    << "or a valid absolute address (0xFFFFFFFF).";
        }
        else
            std::cout << "[error] Please specify address!";
    }
    else if(argv[0] == "breakpoint_disable" || argv[0] == "bd") {
        if(argv.size() > 1)   {  //may change to stoull in future 
            uint64_t addr;
            bool relativeAddr = (!argv[1].empty() && argv[1][0] == '*');
            auto stringViewAddr = stripAddrPrefix(argv[1]);
            
            if(validHexStol(addr, stringViewAddr) && addr < UINT64_MAX - loadAddress_) {
                if(relativeAddr) {
                    addr = addLoadAddress(addr);
                }
                
                auto it = addrToBp_.find(std::bit_cast<intptr_t>(addr));

                if(retAddrFromMain_ && retAddrFromMain_ == &(it->second) && retAddrFromMain_->isEnabled()) {
                    std::cerr << "[warning] Attemping to disable breakpoint at the return address of main. "
                        "Confirm with [y/n]: ";
                    std::string input = "";
                    std::getline(std::cin, input);
                    if(input.length() > 0 && (input[0] == 'y' || input[0] == 'Y')) {
                        std::cerr << "[warning] Breakpoint at return address of main has been disable!\n";
                    }
                    else {
                        std::cout << "[debug] Aborting breakpoint disable.";
                        return true;
                    }
                }
                
                if(it != addrToBp_.end()) {
                    if(it->second.isEnabled()) it->second.disable();

                    std::cout << "[debug] Breakpoint at 0x" << std::hex << it->first 
                        << " (0x" << offsetLoadAddress(addr) << ") is disabled!";
                }
                else {
                    std::cout << "[error] No breakpoint found at address: 0x" << addr;
                    return true;
                }
            }
            else
                std::cout << "[error] Invalid address!\n[info] Pass a valid relative address (*0x1234) "
                    << "or a valid absolute address (0xFFFFFFFF).";
        }
        else
            std::cout << "[error] Please specify address!";
        
    }
    else if(argv[0] == "dump_breakpoints" || argv[0] == "db") {
        std::cout << "[debug] Dumping breakpoints...\n";
        dumpBreakpoints();
    }
    else if(argv[0] == "single_step" || argv[0] == "ss") {
        singleStepBreakpointCheck();
        printSourceAtPC();
        printMemoryLocationAtPC();
    }
    else if(isPrefix(argv[0], "step_in") || argv[0] == "si") {
        stepIn();
        printSourceAtPC();
        printMemoryLocationAtPC();        
    }
    else if(isPrefix(argv[0], "finish")) {
        stepOut();
        printSourceAtPC();
        printMemoryLocationAtPC();        
    }
    else if(isPrefix(argv[0], "next")) {
        stepOver();
        printSourceAtPC();
        printMemoryLocationAtPC();        
    }
    else if(isPrefix(argv[0], "pid")) {
        //std::cout << "[debug] Retrieving child process ID...\n";
        std::cout << std::dec << "[debug] Child process ID = " << getPID();

    }
    else if(argv[0] == "register_read" || argv[0] == "rr") {
        if(argv.size() > 1)  {
            Reg r = getRegFromName(argv[1]);

            if(r == Reg::INVALID_REG) {
                std::cout << "[error] Incorrect Register Name! (ex: r15)";
                return true;
            }
            std::cout << "[debug] " << std::hex << std::uppercase
                << argv[1] << ": 0x" << getRegisterValue(pid_, r);
        } 
        else
            std::cout << "[error] Please specify register!";
    }
    else if(argv[0] == "register_write" || argv[0] == "rw") {
        if(argv.size() > 2)  {
            Reg r = getRegFromName(argv[1]);
            bool relativeAddr = (!argv[2].empty() && argv[2][0] == '*');
            auto stringViewData = stripAddrPrefix(argv[2]);

            if(r == Reg::INVALID_REG) {
                std::cout << "[error] Incorrect register name!";
                return true;
            }

            auto oldVal = getRegisterValue(pid_, r);
            uint64_t data; 
            if(!validHexStol(data, stringViewData)) {
                std::cout << "[error] Please specify valid data!";
                return true;
            }
            if(relativeAddr) {
                if(data > UINT64_MAX - loadAddress_) {
                    std::cout << "[error] Too big for a relative address!";
                    return true;
                }
                data = addLoadAddress(data);
            }
            setRegisterValue(pid_, r, data); 
            auto newReadData = getRegisterValue(pid_, r);

            std::cout << "[debug] " << std::hex << std::uppercase 
                << argv[1] << ": " << oldVal << " --> " << newReadData
                << (relativeAddr ? " (0x" + std::string(stringViewData) + ")" : "");

            if(newReadData != data) {
                std::cerr << "\n[warning] Read register value differs from written value";
            }
        } 
        else
            std::cout << "[error] Please specify register and data!";
    }
    else if(argv[0] == "dump_registers" || argv[0] == "dr") {
        std::cout << "[debug] Dumping registers...\n";
        dumpRegisters();
    }
    else if(argv[0] == "read_memory" || argv[0] == "rm") {
        if(argv.size() > 1)  {
            uint64_t addr;
            bool relativeAddr = (!argv[1].empty() && argv[1][0] == '*');
            auto stringViewAddr = stripAddrPrefix(argv[1]);
            
            if(!validHexStol(addr, stringViewAddr)) {
                std::cout << "[error] Please specify valid address!";
                return true;
            }
            uint64_t data;

            if(relativeAddr) {
                if(addr > UINT64_MAX - loadAddress_) {
                    std::cout << "[error] Too big for a relative address!";
                    return true;
                }
                addr = addLoadAddress(addr);
            }
            
            readMemory(addr, data);
            auto chunk = memMap_.getChunkFromAddr(addr);
            auto mappedSpace = (chunk ? MemoryMap::getFileNameFromChunk(chunk.value()) : "unmapped memory");

            std::cout << std::hex << std::uppercase << "[debug] Read from memory space "
                << mappedSpace << " at 0x" << addr 
                << (relativeAddr ? " (0x" + std::string(stringViewAddr) + ") " : " ") 
                << "--> " << data;
        } 
        else
            std::cout << "[error] Please specify address!";
    }
    else if(argv[0] == "write_memory" || argv[0] == "wm") {
        if(argv.size() > 2)  {
            uint64_t addr;
            uint64_t data;
            bool relativeAddr = (!argv[1].empty() && argv[1][0] == '*');
            auto stringViewAddr = stripAddrPrefix(argv[1]);
            auto stringData = stripAddrPrefix(argv[2]);

            if(!validHexStol(addr, stringViewAddr) || !validHexStol(data, stringData)) {
                std::cout << "[error] Please specify valid memory address and data!";
                return true;
            }
        
            if(relativeAddr) {
                if(addr > UINT64_MAX - loadAddress_) {
                    std::cout << "[error] Too big for a relative address!";
                    return true;
                }
                addr = addLoadAddress(addr);
            }

            uint64_t oldData;
            readMemory(addr, oldData);
            uint64_t newReadData;
            writeMemory(addr, data);
            readMemory(addr, newReadData);
            auto chunk = memMap_.getChunkFromAddr(addr);
            auto mappedSpace = (chunk ? MemoryMap::getFileNameFromChunk(chunk.value()) : "unmapped memory");

            std::cout << std::hex << std::uppercase << "[debug] Wrote to memory space "
                << mappedSpace << " at 0x" << addr
                << (relativeAddr ? " (0x" + std::string(stringViewAddr) + ")" : "") 
                << ": " << oldData << " --> " << newReadData;

            if(newReadData != data) {
                std::cerr << "\n[warning] Read memory value differs from written value";
            }
            
        } 
        else
            std::cout << "[error] Please specify address!";
    }
    else if(argv[0] == "set_context" || argv[0] == "sc") {
        if(argv.size() > 1 && !argv[1].empty()) {
            uint64_t newContext;
            if(!validDecStol(newContext, argv[1]) || newContext > UINT8_MAX || newContext <= 0) {
                std::cout << "[error] Invalid context!\n[info] Context should be from 1-255 inclusive";
                return true;
            }
            std::cout << "[debug] Context: " << std::dec << static_cast<int>(getContext()) << " --> ";
            setContext(static_cast<uint8_t>(newContext));
            std::cout << std::dec << static_cast<int>(getContext());
        }
        else {
            std::cout << "[debug] Number of context lines is currently: " 
                << std::dec << static_cast<int>(getContext());
        }
    }
    else if(argv[0] == "program_counter" || argv[0] == "pc") {
        printSourceAtPC();
        printMemoryLocationAtPC();
    }
    else if(isPrefix(argv[0], "chunk")) {
        std::cout << "[debug] Printing memory chunk...\n";
        uint64_t addr = getPC();
        if(argv.size() > 1) validHexStol(addr, argv[1]);
        memMap_.printChunk(addr);
    }
    else if(argv[0] == "dump_chunks" || argv[0] == "dc") {
        std::cout << "[debug] Dumping all memory chunks...\n";
        memMap_.dumpChunks();
    }
    else if(argv[0] == "dump_symbols" || argv[0] == "ds") {

        if(argv.size() > 1 && argv[1].length() > 0 && !hasWhiteSpace(argv[1])) {
            std::cout << "[debug] Dumping symbol cache for " << argv[1] << " ...";
            symMap_.dumpSymbolCache(argv[1]);
            return true;
        }
        std::cout << "[debug] Dumping all symbol caches...";
        symMap_.dumpSymbolCache();

    }
    else if(argv[0] == "dump_functions" || argv[0] == "df") {
        std::cout << "[debug] Dumping all function DIEs...\n";
        if(argv.size() > 1 && argv[1] == "init") {
            std::cout << "[debug] Re-initializing...\n";
            initializeFunctionDies();

        }
        dumpFunctionDies();
    }
    else if(argv[0] == "help") {
        std::cout << "[info] Welcome to Peek!";
    }
    else if(argv[0] == "q" || argv[0] == "e" || argv[0] == "exit" || argv[0] ==  "quit") {
        std::cout << "[debug] Exiting....";
        state_ = Child::detached;
    }
    else {
        std::cout << "[error] Invalid Command!";
    }

    return true;        //Command processed/spacing needed

}
