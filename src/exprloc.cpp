#include "../include/exprloc.h"
#include "../include/register.h"
#include "../include/debugger.h"

#include <dwarf/dwarf++.hh>

#include <sys/ptrace.h>
#include <bit>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <stdexcept>
#include <iostream>

using namespace reg;


ptrace_expr_context::ptrace_expr_context(pid_t pid, uint64_t loadAddress) : pid_(pid), loadAddress_(loadAddress) {}



dwarf::taddr ptrace_expr_context::reg(unsigned regnum) {
    // std::cout << "regnum, reg: " << std::dec << regnum << ", " << getRegisterName(regnum) << std::endl;
    // auto val = getRegisterValue(pid_, regnum);
    // std::cout << "val: " << std::hex << val << std::endl;
    // return val;
    return getRegisterValue(pid_, regnum);
}

dwarf::taddr ptrace_expr_context::deref_size(dwarf::taddr address, unsigned size) {
    errno = 0;
    long res = ptrace(PTRACE_PEEKDATA, pid_, loadAddress_ + address, nullptr);
    
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
    auto test = data & ((uint64_t(1) << (size*8) ) - 1);
    std::cout << "Size: " << std::dec << size << ", data: "  << data << "\nmask --> " << test << std::endl;
    if(size >= 8) return data;
    
    return data & ((uint64_t(1) << (size*8) ) - 1);    

}

dwarf::taddr ptrace_expr_context::pc() {
    return getRegisterValue(pid_, Reg::rip) - loadAddress_;
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
 
    if (!functionDie.has(dwarf::DW_AT::frame_base)) {
        std::cerr << "[warning] Function has no frame base, cannot evaluate variables\n";
        return;
    }

    size_t varCount = 0;
    // auto base = functionDie[dwarf::DW_AT::frame_base];
    // std::cerr << "DW_AT::frame_base type = " << static_cast<int>(base.get_type()) << std::endl;

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

        std::cout << std::endl;
        for(auto[att, val] : die.attributes()) {
            std::cout << "Att: " << dwarf::to_string(att) << " | val: " << dwarf::to_string(val) << "\n";
        }
        std::cout << std::endl;

        ptrace_expr_context expr(pid_, loadAddress_);
        auto evaluation = loc.as_exprloc().evaluate(&expr);
        std::cerr << "[debug] after evaluation" << std::endl;

        uint64_t val = 0;
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

//TODO:
// figure out type resolver functionality, and return type (may need optional)
// figure out where to interpret bytes as type (may need a seperate function)
Debugger::typeInfo Debugger::typeResolver(const dwarf::die& var) {
    if(!var.has(dwarf::DW_AT::type)) return;

    std::string name = (var.has(dwarf::DW_AT::name) ? dwarf::at_name(var) : "unnamed");
    auto die = var;
    auto typeDie = die[dwarf::DW_AT::type];

}
