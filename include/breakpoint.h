#pragma once

#include <sys/types.h>
#include <cstdint>

//#include "register.h"

class Breakpoint {
    pid_t pid_;
    std::intptr_t addr_;
    bool enabled_;
    std::uint8_t data_;
    
    static constexpr std::uint8_t mask_ = 0xFF;

public:
    Breakpoint(pid_t pid, std::intptr_t addr);

    bool isEnabled() const;
    std::uint8_t getData();

    bool enable();
    bool disable();

};