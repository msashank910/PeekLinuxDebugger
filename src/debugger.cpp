#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <errno.h>
#include <vector>
#include <linenoise.h>
#include <unordered_map>
#include <cstdint>

//cmake --build build to compile if changed this file
// cmake -B build . along with command above if made changes to CMAKE settings
//rm -rf build along with commands above for full clean rebuild
//create bash script for this later


//Helpers
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
class Debugger {
    pid_t _pid;
    std::string _progName;
    std::unordered_map<std::intptr_t, Breakpoint> _addrToBp;

    void handleCommand(std::string args);

public:
    Debugger(int _pid, std::string _progName);
    void run();
    
    void setBreakpointAtAddress(std::intptr_t address);
    
};

class Breakpoint {
    bool _enabled;
    std::intptr_t _addr;
    std::uint8_t _data;
    pid_t _pid;
    

public:
    Breakpoint(pid_t pid, std::intptr_t addr);

    bool isEnabled();
    std::uint8_t getData();

    void enable();
    void disable();

};

//Debugger Methods
Debugger::Debugger(pid_t pid, std::string progName) : _pid(pid), _progName(progName) {}

void Debugger::run() {
    char* line;

    while((line = linenoise("[__pld__] ")) != nullptr) {
        handleCommand(line);
        linenoiseHistoryAdd(line);  //may need to initialize history
        linenoiseFree(line);
    }
}

void Debugger::handleCommand(std::string args) {
    auto argv = splitLine(args, ' ');

    if(isPrefix(argv[0], "continue")) {
        std::cout << "Continue Execution\n";
    }
    else if(isPrefix(argv[0], "break")) {
        std::cout << "Placing breakpoint\n";
    }
    else {
        std::cout << "Invalid Command!\n";
    }
}

void Debugger::setBreakpointAtAddress(std::intptr_t address) {    
    std::cout << "Setting Breakpoint at: 0x" << std::hex << address << "\n";    //changed to hex
    Breakpoint bp = Breakpoint(_pid, address);
    bp.enable();
    
    _addrToBp[address] = bp;
    std::cout << "Breakpoint successfully set!\n";
}


//Breakpoint Methods
Breakpoint::Breakpoint(pid_t pid, std::intptr_t addr) : _pid(pid), _addr(addr) {}

bool Breakpoint::isEnabled() {return _enabled;}
std::uint8_t Breakpoint::getData() {return _data;}


void Breakpoint::enable() { //TODO
    

    _enabled = true;
}

void Breakpoint::disable() {
    

    _enabled = false;
}


//Main Driver
int main(int argc, char* argv[]) {

    if(argc < 2) {
        std::cerr << "Must specify Program name\n";
        return 1;
    }
    
    auto progName = argv[1];
    auto pid = fork();  // runs a child process concurrently to this process
    
    if(pid == 0) {
        std::cout << "Entering child process....\n";

        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
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