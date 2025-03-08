#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/personality.h>

#include "../include/debugger.h"

//cmake --build build to compile if changed this file
// cmake -B build . along with command above if made changes to CMAKE settings
//rm -rf build along with commands above for full clean rebuild
//create bash script for this later


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