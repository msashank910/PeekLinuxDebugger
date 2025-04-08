#pragma once

#include <string>
#include <string_view>
#include <array>
#include <cstdint>
#include <sys/user.h>



namespace reg {		//register is a reserved keyword
	enum class Reg {
		r15,
		r14,
		r13,  
		r12,
		rbp,
		rbx,
		r11,
		r10,
		r9,
		r8,
		rax,
		rcx,
		rdx,
		rsi,
		rdi,
		orig_rax,
		rip,
		cs,
		eflags,
		rsp,
		ss,
		fs_base,
		gs_base,
		ds,
		es,
		fs,
		gs,
		INVALID_REG
	  };
	  
	inline constexpr int registerCount_ = 27;

	struct regDescriptor {
		const int dwarfNum;
		const std::string regName;
		const Reg r;
	};

	extern const std::array<regDescriptor, 27> regDescriptorList;


	bool setRegisterValue(pid_t pid, const Reg r, uint64_t val);
    uint64_t getRegisterValue(const pid_t pid, const Reg r);
    uint64_t getRegisterValue(const pid_t pid, const int dwarfNum);

    std::string getRegisterName(const Reg r);
    Reg getRegFromName(const std::string_view regName);
	bool getAllRegisterValues(const pid_t pid, user_regs_struct& rawRegVals);
	bool setAllRegisterValues(const pid_t pid, user_regs_struct& rawRegVals);
}

  