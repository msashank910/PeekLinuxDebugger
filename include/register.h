#pragma once

#include <string>
#include <array>

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
		gs
	  };
	  
	inline constexpr int registerCount_ = 27;

	struct regDescriptor {
		int dwarfNum;
		std::string regName;
		Reg r;
	};

	extern std::array<regDescriptor, 27> regDescriptorList;
}

  