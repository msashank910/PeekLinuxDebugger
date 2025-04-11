#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <signal.h>
#include <sys/personality.h>

#include "../include/debugger.h"
#include "../include/constants.h"

using constants::STOPWAIT_SIGNAL;

//cmake --build build to compile if changed this file
// cmake -B build . along with command above if made changes to CMAKE settings
//rm -rf build along with commands above for full clean rebuild
//create bash script for this later


//Main Driver
int main(int argc, char* argv[]) {

    if(argc < 2) {
        std::cerr << "[fatal] Must specify Program name\n";
        return 1;
    }
    
    auto progName = argv[1];

    //Masks STOPWAIT_SIGNAL
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGWINCH);
    sigprocmask(SIG_BLOCK, &mask, nullptr);

    pid_t pid = fork();  // runs a child process concurrently to this process
    
    if(pid == 0) {
        std::cout << "[debug] Entering child process....\n";
        
        //Attach to debugger
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);

        //Ignores SIGWINCH signals
        // struct sigaction sa{};
        // sa.sa_handler = SIG_IGN;
        // sigaction(SIGWINCH, &sa, nullptr);

        
        
        //Doesn't randomize load address, excecutes child program
        personality(ADDR_NO_RANDOMIZE);
        execl(progName, progName, nullptr); //second arg declares name as ./prog
        
        std::cerr << "\n[fatal] The debuggee argument is invalid: " << strerror(errno) << "\n";
        //perror("The debuggee argument is invalid");
        return 1;
        
    }
    else if(pid < 0) {
        std::cerr << "[fatal] Invalid Fork\n";
        return 1;
    }
    
    //run debugger (if program reached here it is a parent)
    std::cout << "[debug] Entering parent debugger process....\n";
    
    Debugger debug(pid, progName);
    debug.run();

    return 0;
}