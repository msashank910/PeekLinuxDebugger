#include "../include/breakpoint.h"

#include <sys/ptrace.h>
#include <sys/types.h>
#include <cstdint>


//Breakpoint Methods
Breakpoint::Breakpoint(pid_t pid, std::intptr_t addr) : pid_(pid), addr_(addr), enabled_(false), data_(0) {}
bool Breakpoint::isEnabled() {return enabled_;}     
std::uint8_t Breakpoint::getData() {return data_;}  

void Breakpoint::enable() { //Optimize?
    constexpr std::uint64_t int3 = 0xcc;
    auto word = ptrace(PTRACE_PEEKDATA, addr_, 0, nullptr);
    
    //Linux is little endian, LSB is first. 0xFF -> 0000 ...00 1111 1111
    data_ = static_cast<std::uint8_t>(word & mask_);
    word = (word & ~mask_) | int3;

    ptrace(PTRACE_POKEDATA, pid_, addr_, word, nullptr);
    enabled_ = true;
}

void Breakpoint::disable() {
    auto word = ptrace(PTRACE_PEEKDATA, addr_, 0, nullptr);
    word = ((word & ~mask_) | data_);

    ptrace(PTRACE_POKEDATA, pid_, addr_, word, nullptr);
    enabled_ = false;
}