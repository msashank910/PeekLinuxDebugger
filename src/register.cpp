#include "../include/register.h"

#include <array>
#include <string>

namespace reg {
    std::array<regDescriptor, 27> regDescriptorList = {{
        {15, "r15", Reg::r15},
		{14, "r14", Reg::r14},
		{13, "r13", Reg::r13},  
		{12, "r12", Reg::r12},
		{6, "rbp", Reg::rbp},
		{3, "rbx", Reg::rbx},
		{11, "r11", Reg::r11},
		{10, "r10", Reg::r10},
		{9, "r9", Reg::r9},
		{8, "r8", Reg::r8},
		{0, "rax", Reg::rax},  
		{2, "rcx", Reg::rcx},
		{1, "rdx", Reg::rdx},
		{4, "rsi", Reg::rsi},
		{5, "rdi", Reg::rdi},
		{-1, "orig_rax", Reg::orig_rax},    //Doesnt have a dwarf
		{-1, "rip", Reg::rip},               //Doesnt have a dwarf
		{51, "cs", Reg::cs},
		{49, "eflags", Reg::eflags},        //Under rflags
		{7, "rsp", Reg::rsp},
		{52, "ss", Reg::ss},
		{58, "fs_base", Reg::fs_base},
		{59, "gs_base", Reg::gs_base},
		{53, "ds", Reg::ds},
		{50, "es", Reg::es},
		{54, "fs", Reg::fs},
		{55, "gs", Reg::gs}
    }};
}