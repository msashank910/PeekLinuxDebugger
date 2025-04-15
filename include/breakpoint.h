#pragma once

#include <sys/types.h>
#include <cstdint>


class Breakpoint {
    pid_t pid_ = 0;
    std::intptr_t addr_ = 0;
    bool enabled_ = 0;
    std::uint8_t data_ = 0;
    
    static constexpr std::uint8_t mask_ = 0xFF;

public:
    Breakpoint() = default;
    Breakpoint(pid_t pid, std::intptr_t addr);
    
    Breakpoint(Breakpoint&&) = default;
    Breakpoint& operator=(Breakpoint&&) = default;
    ~Breakpoint() = default;
    
    Breakpoint(const Breakpoint&) = delete;
    Breakpoint& operator=(const Breakpoint&) = delete;

    bool isEnabled() const;
    std::uint8_t getData() const;
    std::intptr_t getAddr() const;

    bool enable();
    bool disable();

};