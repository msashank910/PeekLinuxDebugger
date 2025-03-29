#include "../include/debugger.h"
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


//using namespaces util and reg
using namespace util;
using namespace reg;

//Debugger function: run()

void Debugger::run() {
    waitForSignal();
    initializeMemoryMapAndLoadAddress();

    char* line;
    std::string prevArgs = "";
    while(!exit_ && (line = linenoise("[__pld__] ")) != nullptr) {
        if(handleCommand(line, prevArgs)) {std::cout << std::endl;}
        linenoiseHistoryAdd(line);  //may need to initialize history
        linenoiseFree(line);
    }
}

//Debugger function: handleCommand()

bool Debugger::handleCommand(const std::string& args, std::string& prevArgs) {
    if(args.empty() && prevArgs.empty()){
        //std::cout << "args empty in handlecommand\n";
        return false;
    }

    std::vector<std::string> argv;
    if (args.empty()) argv = splitLine(prevArgs, ' ');      //fix cache logic
    else argv = splitLine(args, ' ');
    if(argv.empty()) return false;
    
    if(!args.empty()) prevArgs = args;

    if(isPrefix(argv[0], "continue_execution")) {
        std::cout << "Continue Execution..." << std::endl;
        continueExecution();
        //return false;
    }
    else if(argv[0] == "debug") {
        //print memmap
        memMap_.printChunks();
    }
    else if(isPrefix(argv[0], "breakpoint")) {
        //std::cout << "Placing breakpoint\n";
        if(argv.size() > 1)   {  //may change to stoull in future 
            uint64_t addr;
            bool relativeAddr = (!argv[1].empty() && argv[1][0] == '*');
            auto stringViewAddr = stripAddrPrefix(argv[1]);
            
            if(validHexStol(addr, stringViewAddr) && addr < UINT64_MAX - loadAddress_) {
                //std::cout << "passed validHexStol in breakpoint\n";
                if(relativeAddr) {
                    //std::cout << "load address added\n";
                    addr = addLoadAddress(addr);
                }

                std::cout << "Setting Breakpoint at: 0x" << std::hex << addr
                    << (relativeAddr ? " (0x" + std::string(stringViewAddr) + ")\n" : "\n");

                setBreakpointAtAddress(std::bit_cast<intptr_t>(addr));
            }
            else
                std::cout << "Invalid address!\nPass a valid relative address (*0x1234) "
                    << "or a valid absolute address (0xFFFFFFFF).";
        }
        else
            std::cout << "Please specify address!";
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

                    std::cout << "Breakpoint at 0x" << std::hex << it->first 
                        << " (0x" << offsetLoadAddress(addr) << ") is enabled!";
                }
                else {
                    std::cout << "No breakpoint found at address: 0x" << addr;
                    return true;
                }
            }
            else
                std::cout << "Invalid address!\nPass a valid relative address (*0x1234) "
                    << "or a valid absolute address (0xFFFFFFFF).";
        }
        else
            std::cout << "Please specify address!";
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
                
                if(it != addrToBp_.end()) {
                    if(it->second.isEnabled()) it->second.disable();

                    std::cout << "Breakpoint at 0x" << std::hex << it->first 
                        << " (0x" << offsetLoadAddress(addr) << ") is disabled!";
                }
                else {
                    std::cout << "No breakpoint found at address: 0x" << addr;
                    return true;
                }
            }
            else
                std::cout << "Invalid address!\nPass a valid relative address (*0x1234) "
                    << "or a valid absolute address (0xFFFFFFFF).";
        }
        else
            std::cout << "Please specify address!";
        
    }
    else if(argv[0] == "dump_breakpoints" || argv[0] == "db") {
        std::cout << "Dumping breakpoints...\n";
        dumpBreakpoints();
    }
    else if(argv[0] == "single_step" || argv[0] == "ss") {
        singleStepBreakpointCheck();

        auto pc = getPC();
        auto pcOffset = offsetLoadAddress(pc);
    
        std::cout << std::hex << std::uppercase << "Currently at PC: 0x" << pc
             << " (0x" << pcOffset << ")\n";

        //printSourceAtPC();
    }
    else if(isPrefix(argv[0], "step_in") || argv[0] == "si") {
        stepIn();

        auto pc = getPC();
        auto pcOffset = offsetLoadAddress(pc);
    
        std::cout << std::hex << std::uppercase << "Currently at PC: 0x" << pc
             << " (0x" << pcOffset << ")\n";
        
        //printSourceAtPC();
    }
    else if(isPrefix(argv[0], "finish")) {
        stepOut();

        auto pc = getPC();
        auto pcOffset = offsetLoadAddress(pc);
    
        std::cout << std::hex << std::uppercase << "Currently at PC: 0x" << pc
             << " (0x" << pcOffset << ")\n";

        //printSourceAtPC();
    }
    else if(isPrefix(argv[0], "next")) {
        stepOver();

        auto pc = getPC();
        auto pcOffset = offsetLoadAddress(pc);
    
        std::cout << std::hex << std::uppercase << "Currently at PC: 0x" << pc
             << " (0x" << pcOffset << ")\n";

        //printSourceAtPC();
    }
    else if(isPrefix(argv[0], "pid")) {
        std::cout << "Retrieving child process ID...\n";
        std::cout << std::dec << "pID = " << getPID();

    }
    else if(argv[0] == "register_read" || argv[0] == "rr") {
        if(argv.size() > 1)  {
            Reg r = getRegFromName(argv[1]);

            if(r == Reg::INVALID_REG) {
                std::cout << "Incorrect Register Name! (ex: r15)";
                return true;
            }
            std::cout << std::hex << std::uppercase
                << argv[1] << ": 0x" << getRegisterValue(pid_, r);
        } 
        else
            std::cout << "Please specify register!";
    }
    else if(argv[0] == "register_write" || argv[0] == "rw") {
        if(argv.size() > 2)  {
            Reg r = getRegFromName(argv[1]);
            bool relativeAddr = (!argv[2].empty() && argv[2][0] == '*');
            auto stringViewData = stripAddrPrefix(argv[2]);

            if(r == Reg::INVALID_REG) {
                std::cout << "Incorrect register name!";
                return true;
            }

            auto oldVal = getRegisterValue(pid_, r);
            uint64_t data; 
            if(!validHexStol(data, stringViewData)) {
                std::cout << "Please specify valid data!";
                return true;
            }
            if(relativeAddr) {
                if(data > UINT64_MAX - loadAddress_) {
                    std::cout << "Too big for a relative address!";
                    return true;
                }
                data = addLoadAddress(data);
            }
            setRegisterValue(pid_, r, data); 
            auto newReadData = getRegisterValue(pid_, r);

            std::cout << std::hex << std::uppercase 
                << argv[1] << ": " << oldVal << " --> " << newReadData
                << (relativeAddr ? " (0x" + std::string(stringViewData) + ")" : "");

            if(newReadData != data) {
                std::cerr << "\n**WARNING: Read register value differs from written value**";
            }
        } 
        else
            std::cout << "Please specify register and data!";
    }
    else if(argv[0] == "dump_registers" || argv[0] == "dr") {
        std::cout << "Dumping registers...\n";
        dumpRegisters();
    }
    else if(argv[0] == "read_memory" || argv[0] == "rm") {
        if(argv.size() > 1)  {
            uint64_t addr;
            bool relativeAddr = (!argv[1].empty() && argv[1][0] == '*');
            auto stringViewAddr = stripAddrPrefix(argv[1]);
            
            if(!validHexStol(addr, stringViewAddr)) {
                std::cout << "Please specify valid address!";
                return true;
            }
            uint64_t data;

            if(relativeAddr) {
                if(addr > UINT64_MAX - loadAddress_) {
                    std::cout << "Too big for a relative address!";
                    return true;
                }
                addr = addLoadAddress(addr);
            }
            
            readMemory(addr, data);
            std::cout << std::hex << std::uppercase << "Read from memory at 0x" << addr 
                << (relativeAddr ? " (0x" + std::string(stringViewAddr) + ") " : " ") 
                << "--> " << data;
        } 
        else
            std::cout << "Please specify address!";
    }
    else if(argv[0] == "write_memory" || argv[0] == "wm") {
        if(argv.size() > 2)  {
            uint64_t addr;
            uint64_t data;
            bool relativeAddr = (!argv[1].empty() && argv[1][0] == '*');
            auto stringViewAddr = stripAddrPrefix(argv[1]);
            auto stringData = stripAddrPrefix(argv[2]);

            if(!validHexStol(addr, stringViewAddr) || !validHexStol(data, stringData)) {
                std::cout << "Please specify valid memory address and data!";
                return true;
            }
        
            if(relativeAddr) {
                if(addr > UINT64_MAX - loadAddress_) {
                    std::cout << "Too big for a relative address!";
                    return true;
                }
                addr = addLoadAddress(addr);
            }

            uint64_t oldData;
            readMemory(addr, oldData);
            uint64_t newReadData;
            writeMemory(addr, data);
            readMemory(addr, newReadData);

            std::cout << std::hex << std::uppercase << "Memory at 0x" << addr
                << (relativeAddr ? " (0x" + std::string(stringViewAddr) + ")" : "") 
                << ": " << oldData << " --> " << newReadData;

            if(newReadData != data) {
                std::cerr << "\n**WARNING: Read memory value differs from written value**";
            }
            
        } 
        else
            std::cout << "Please specify address!";
    }
    else if(argv[0] == "set_context" || argv[0] == "sc") {
        if(argv.size() > 1 && !argv[1].empty()) {
            uint64_t newContext;
            if(!validDecStol(newContext, argv[1]) || newContext > UINT8_MAX || newContext <= 0) {
                std::cout << "Context should be from 1-255 inclusive";
                return true;
            }
            std::cout << "Context: " << std::dec << static_cast<int>(getContext()) << " --> ";
            setContext(static_cast<uint8_t>(newContext));
            std::cout << std::dec << static_cast<int>(getContext());
        }
        else {
            std::cout << "Number of context lines is currently: " 
                << std::dec << static_cast<int>(getContext());
        }
    }
    else if(argv[0] == "program_counter" || argv[0] == "pc") {
        auto pc = getPC();
        auto pcOffset = offsetLoadAddress(pc);
    
        std::cout << std::hex << std::uppercase << "Retrieving program counter...\n" 
            << "Program Counter (rip): 0x" << pc << " (0x" << pcOffset << ")";
    }
    else if(argv[0] == "help") {
        std::cout << "Welcome to Peek!";
    }
    else if(argv[0] == "q" || argv[0] == "e" || argv[0] == "exit" || argv[0] ==  "quit") {
        std::cout << "Exiting....";
        exit_ = true;
    }
    else {
        std::cout << "Invalid Command!";
    }

    return true;        //Command processed/spacing needed

}
