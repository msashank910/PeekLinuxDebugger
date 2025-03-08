#include "../include/register.h"

#include <array>
#include <string>
#include <sys/user.h>
#include <sys/ptrace.h>
#include <cstdint>
#include <algorithm>

namespace reg {
    const std::array<regDescriptor, 27> regDescriptorList = {{
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

	bool setRegisterValue(pid_t pid, Reg r, uint64_t val) {
		user_regs_struct regVals;
		ptrace(PTRACE_GETREGS, pid, nullptr, &regVals);

		auto it =
			std::find_if(regDescriptorList.begin(), regDescriptorList.end(), [r](auto&& rd) {
				return r == rd.r;
			}
		);
		*(reinterpret_cast<uint64_t*>(&regVals) + (it - regDescriptorList.begin())) = val;

		return !(ptrace(PTRACE_SETREGS, pid, nullptr, &regVals));
	}

    uint64_t getRegisterValue(const pid_t pid, const Reg r) {
		user_regs_struct regVals;
		ptrace(PTRACE_GETREGS, pid, nullptr, &regVals);

		//auto&& -> reference to const regDescriptor struct
		auto it =
			std::find_if(regDescriptorList.begin(), regDescriptorList.end(), [r](auto&& rd) {
				return r == rd.r;
			}
		);
		return *(reinterpret_cast<uint64_t*>(&regVals) + (it - regDescriptorList.begin()));
	}

    uint64_t getRegisterValue(const pid_t pid, const int dwarfNum) {
		user_regs_struct regVals;
		ptrace(PTRACE_GETREGS, pid, nullptr, &regVals);

		//auto&& -> reference to const regDescriptor struct
		auto it =
			std::find_if(regDescriptorList.begin(), regDescriptorList.end(), [dwarfNum](auto&& rd) {
				return dwarfNum == rd.dwarfNum;
			}
		);
		return *(reinterpret_cast<uint64_t*>(&regVals) + (it - regDescriptorList.begin()));
	}

    uint64_t getRegisterValue(const pid_t pid, const std::string& regName) {
		user_regs_struct regVals;
		ptrace(PTRACE_GETREGS, pid, nullptr, &regVals);

		//auto&& -> reference to const regDescriptor struct
		auto it =
			std::find_if(regDescriptorList.begin(), regDescriptorList.end(), [regName](auto&& rd) {
				return regName == rd.regName;
			}
		);
		return *(reinterpret_cast<uint64_t*>(&regVals) + (it - regDescriptorList.begin()));

	}

    std::string getRegisterName(const Reg r) {
		return std::find_if(regDescriptorList.begin(), regDescriptorList.end(), [r](auto&& rd){
			return rd.r == r;
		})->regName;
	}

    Reg getRegFromName(const std::string& regName) {
		return std::find_if(regDescriptorList.begin(), regDescriptorList.end(), [regName](auto&& rd){
			return rd.regName == regName;
		})->r;
	}

	uint64_t* getAllRegisterValues(const pid_t pid, user_regs_struct& rawRegVals) {
		ptrace(PTRACE_GETREGS, pid, nullptr, &rawRegVals);
		return reinterpret_cast<uint64_t*>(&rawRegVals);	//dangling pointer if regVals is not a parameter!
	}
}