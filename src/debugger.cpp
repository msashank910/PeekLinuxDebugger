#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <errno.h>
#include <vector>
#include <linenoise.h>
#include <unordered_map>
#include <cstdint>
#include <sys/personality.h>

//cmake --build build to compile if changed this file
// cmake -B build . along with command above if made changes to CMAKE settings
//rm -rf build along with commands above for full clean rebuild
//create bash script for this later


//Helpers 
std::string strip0x(const std::string& s) {          //maybe change later
    if(!s.empty() && s.length() > 2 && s[0] == '0' && s[1] == 'x') {
        return std::string(s.begin() + 2, s.end());
    }
    return s;
}

bool isPrefix(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin());
}

std::vector<std::string> splitLine(const std::string &line, char delimiter) {    
    std::vector<std::string> args;
    std::stringstream ss(line);
    std::string temp;

    while(getline(ss, temp, delimiter)) {
        args.push_back(temp);
    }

    return args;
}



//Classes
class Breakpoint {
    pid_t pid_;
    std::intptr_t addr_;
    bool enabled_;
    std::uint8_t data_;
    
    static constexpr std::uint8_t mask_ = 0xFF;

public:
    Breakpoint(pid_t pid, std::intptr_t addr);

    bool isEnabled();
    std::uint8_t getData();

    void enable();
    void disable();

};


class Debugger {
    pid_t pid_;
    std::string progName_;
    std::unordered_map<std::intptr_t, Breakpoint> addrToBp_;

    void handleCommand(std::string args);

public:
    Debugger(pid_t pid, std::string progName);
    int getPID();
    void run();
    void continueExecution();
    void setBreakpointAtAddress(std::intptr_t address);
    
};



//Debugger Methods
Debugger::Debugger(pid_t pid, std::string progName) : pid_(pid), progName_(progName) {}

void Debugger::run() {
    int waitStatus;
    auto options = 0;
    waitpid(pid_, &waitStatus, options);        //may simiplify later

    char* line;

    while((line = linenoise("[__pld__] ")) != nullptr) {
        handleCommand(line);
        linenoiseHistoryAdd(line);  //may need to initialize history
        linenoiseFree(line);
    }
}

int Debugger::getPID() {return pid_;}

void Debugger::handleCommand(std::string args) {
    auto argv = splitLine(args, ' ');

    if(isPrefix(argv[0], "continue")) {
        std::cout << "Continue Execution\n";
    }
    else if(isPrefix(argv[0], "break")) {
        //std::cout << "Placing breakpoint\n";
        if(argv.size() > 1)
            setBreakpointAtAddress(std::stol(strip0x(argv[1]), nullptr, 16));
        else
            std::cout << "Please specify address!\n";
    }
    else if(isPrefix(argv[0], "pid")) {
        std::cout << "Retrieving child process ID...\n";
        std::cout << "pID = " << getPID() << "\n";

    }
    else {
        std::cout << "Invalid Command!\n";
    }
}

void Debugger::setBreakpointAtAddress(std::intptr_t address) {    
    std::cout << "Setting Breakpoint at: 0x" << std::hex << address << "\n";    //changed to hex
    
    auto [it, inserted] = addrToBp_.emplace(address, Breakpoint(pid_, address));
    if(inserted) {
        it->second.enable();
        std::cout << "Breakpoint successfully set!\n";
    }
    else {
        std::cout << "Breakpoint already exists!\n";  
    }

}

void Debugger::continueExecution() {
    ptrace(PTRACE_CONT, pid_, nullptr, nullptr);

    int waitStatus;
    auto options = 0;
    waitpid(pid_, &waitStatus, options);
}



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


//Main Driver
int main(int argc, char* argv[]) {

    if(argc < 2) {
        std::cerr << "Must specify Program name\n";
        return 1;
    }
    
    auto progName = argv[1];
    pid_t pid = fork();  // runs a child process concurrently to this process
    
    if(pid == 0) {
        std::cout << "Entering child process....\n";

        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        personality(ADDR_NO_RANDOMIZE);
        execl(progName, progName, nullptr); //second arg declares name as ./prog
        
        perror("The debuggee argument is invalid");
        return 1;
        
    }
    else if(pid < 0) {
        std::cerr << "Invalid Fork\n";
        return 1;
    }
    
    //run debugger (if program reached here it is a parent)
    std::cout << "Entering parent debugger process:\n";

    Debugger debug(pid, progName);
    debug.run();

    return 0;
}