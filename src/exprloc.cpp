#include "../include/exprloc.h"
#include "../include/register.h"
#include "../include/debugger.h"

#include <dwarf/dwarf++.hh>

#include <sys/ptrace.h>
#include <bit>
#include <string>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <iostream>

using namespace reg;


ptrace_expr_context::ptrace_expr_context(pid_t pid) : pid_(pid) {}



dwarf::taddr ptrace_expr_context::reg(unsigned regnum) {
    // std::cout << "regnum: " << std::dec << regnum << std::endl;
    return getRegisterValue(pid_, regnum);
}

dwarf::taddr ptrace_expr_context::deref_size(dwarf::taddr address, unsigned size) {
    errno = 0;
    long res = ptrace(PTRACE_PEEKDATA, pid_, address, nullptr);
    
    if(res == -1 && errno) {
        throw std::runtime_error("\n[fatal] In ptrace_expr_context::deref_size - ptrace error: " 
			+ std::string(std::strerror(errno)) + ".\n[fatal] Check Memory Address!\n");
    }

    /*
        1) Bit cast a int64_t to a uint64_t.
        2) Retrieve number of bits from number of bytes (size*8)
        3) Retrieve 1 << bits, this is 1 past the bitmask we need.
            -> For 3 bits: 1 << bits = 0b1000 (8 in binary)
        4) Subtract one from the bitmask to retrieve target mask.
            -> For (1 << 3) - 1: 0b1000 - 0b0001 = 0b0111
            -> Notice the number of 1s in resulting bitmask is equal to number of bits
        5) Use bitwise AND operator (&) on data and mask to isolate desired bits, beginning with the MSB
            -> For data = 0xCBA and mask = 0xF (0b1111): data & mask = 0xA
        
    */
    auto data = std::bit_cast<dwarf::taddr>(res);
    if(size >= 8) return data;
    
    return data & ((uint64_t(1) << (size*8) ) - 1);    

}

dwarf::taddr ptrace_expr_context::pc() {
    return getRegisterValue(pid_, Reg::rip);
}


/*
    Debugger member functions that handle expression evaluation/variable resolution/manipulation
*/

void Debugger::readVariable(const std::string& name) const {

}

void Debugger::writeVariable(const std::string& var, const std::string& val) {

}

void Debugger::getVariables(const uint64_t pc) const {

}

void Debugger::dumpVariables() const {
    auto func = getFunctionFromPCOffset(getPCOffsetAddress());
    if(!func) {
        std::cerr << "[error] Cannot dump variables in non-user written function.\n";
        return;
    }

    auto functionDie = func.value();
    size_t varCount = 0;

    std::cout << "\n--------------------------------------------------------\n";

    for(const auto& die : functionDie) {
        if(die.tag != dwarf::DW_TAG::variable || !die.has(dwarf::DW_AT::location)) continue;
        varCount++;
        
        auto loc = die[dwarf::DW_AT::location];
        auto name = (die.has(dwarf::DW_AT::name) ? dwarf::at_name(die) : "variable");

        if(loc.get_type() != dwarf::value::type::exprloc) {
            //debug message
            std::cout << "(" << std::dec << varCount << ") " << name
                << " is not an exprloc\n";
            continue;
        }

        ptrace_expr_context expr(pid_);
        std::cerr << "prior to evaluation" << std::endl;
        auto evaluation = loc.as_exprloc().evaluate(&expr);
        std::cerr << "after evaluation" << std::endl;
        uint64_t val;
        std::cout << "(" << std::dec << varCount << ") " << name;

        switch(evaluation.location_type) {
            
            case dwarf::expr_result::type::address:
                readMemory(evaluation.value, val);
                std::cout << " in memory at 0x" << std::hex << std::uppercase
                    << evaluation.value << ": " << std::dec << val << "\n";
                break;

            case dwarf::expr_result::type::reg:
                val = getRegisterValue(pid_, evaluation.value);
                std::cout << " in register " << getRegisterName(evaluation.value) << ": "
                    << std::dec << val << "\n";
                break;

            default:
                std::cout << " is not stored in a memory address or register\n";
                break;
        }

    }
    std::cout << "\n--------------------------------------------------------\n";
}

