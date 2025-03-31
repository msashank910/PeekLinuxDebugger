#include "../include/breakpoint.h"

#include <sys/ptrace.h>
#include <sys/types.h>
#include <cstdint>
#include <cstring> 
#include <cerrno>
#include <iostream>


//Breakpoint Methods
Breakpoint::Breakpoint(pid_t pid, std::intptr_t addr) : pid_(pid), addr_(addr), enabled_(false), data_(0) {}
bool Breakpoint::isEnabled() const {return enabled_;}     
std::uint8_t Breakpoint::getData() const {return data_;}  

bool Breakpoint::enable() { //Optimize?     //fixed wrong parameters for both ptrace calls
    constexpr std::uint64_t int3 = 0xcc;
    auto word = ptrace(PTRACE_PEEKDATA, pid_, addr_, nullptr);
    
    //Linux is little endian, LSB is first. 0xFF -> 0000 ...00 1111 1111
    data_ = static_cast<std::uint8_t>(word & mask_);
    word = (word & ~mask_) | int3;

    errno = 0;
    long res = ptrace(PTRACE_POKEDATA, pid_, addr_, word);

    if(res == -1) {
        std::cerr << "[critical] Enable Breakpoint has failed: " << strerror(errno) << "\n";
        return false;
    }

    enabled_ = true;
    return true;
}

bool Breakpoint::disable() {    //fixed wrong parameters for both ptrace calls
    auto word = ptrace(PTRACE_PEEKDATA, pid_, addr_, nullptr);  
    word = ((word & ~mask_) | data_);

    errno = 0;
    long res = ptrace(PTRACE_POKEDATA, pid_, addr_, word);

    if(res == -1) {
        std::cerr << "[critical] Disable Breakpoint has failed: " << strerror(errno) << "\n";
        return false;
    }

    enabled_ = false;
    return true;
}