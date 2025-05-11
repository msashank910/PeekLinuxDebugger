#pragma once

#include <sys/types.h>
#include <cstdint>

#include <dwarf/dwarf++.hh>


class ptrace_expr_context : public dwarf::expr_context {
    pid_t pid_;
    uint64_t loadAddress_;
    
    
public:
    ptrace_expr_context(pid_t pid, uint64_t loadAddress);
    
    dwarf::taddr reg(unsigned regnum) override;
    dwarf::taddr deref_size(dwarf::taddr address, unsigned size) override;
    dwarf::taddr pc() override;

    // may implement later
    
    // Calculates real mem address for var stored in thread local storage (tls)
    // dwarf::taddr form_tls_address(dwarf::taddr address) override;

    // Dereference memory from a given address space ID (ASID)
    // dwarf::taddr xderef_size(dwarf::taddr address, dwarf::taddr asid, unsigned size) override;
};
