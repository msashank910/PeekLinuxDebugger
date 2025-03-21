Peek - A Lightweight Linux Debugger (WIP)

Peek is a lightweight Linux debugger for 64-bit systems, built from scratch in C++. This project is currently a work in progress and aims to provide essential debugging features with a minimalistic approach. It uses Linenoise for command line editing and Libelfin for parsing ELF and DWARF debugging information.

Features (Implemented)

- Attaching to processes
- Setting breakpoints
- Inspecting registers
- Memory manipulation
- Single-Step

Technologies Used:

    Language: C++
    Platform: Linux-x86_64 (WSL2 compatible)
    Build System: g++ / CMake

📦 Installation

Clone the repository:

    git clone https://github.com/msashank910/Peek.git
    cd Peek

Build the project:

    mkdir -p build && cmake -B build . && cmake --build build

🖥 Usage

To run the debugger:
    
    ./build/pld ./<file_path_to_debuggee_program>

Note: Debugee Program must be compiled with the following flags

    -g -gdwarf04

While in the [_pld_] command-line interface, you can interact with the child process via breakpoints, memory manipulation, and register manipulation.
For a full list of commands, use the help command:

    help

Commands can be executed via their prefixes as well
For example for continue: cont, con, c all work.

📋 Current Limitations

    No GUI (command-line only).
    Limited command support (in progress).
    Potentially requires sudo for ptrace permissions.

🛤 Roadmap

Elves and Dwarves

    Implement support for ELF (Executable and Linkable Format) files and DWARF debugging symbols to enable accurate symbol resolution and debugging information.
    
Source and Signals

    Handle and interpret UNIX signals during debugging (e.g., SIGINT, SIGSEGV).
    Map signals to user-friendly messages and responses.

Source-Level Stepping

    Allow line-by-line execution and stepping through source code rather than assembly instructions.

Source-Level Breakpoints

    Enable breakpoints to be set directly at specific lines of code rather than just memory addresses.

Stack Unwinding

    Implement stack trace generation to unwind the stack and show function call history.

Handling Variables

    Inspect and modify local variables and function parameters during execution.

    Documentation and examples

🤝 Contributing

Contributions are welcome! Feel free to fork the repository and create pull requests.

📄 License

This project is licensed under the MIT License.
