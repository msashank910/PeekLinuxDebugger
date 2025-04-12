#pragma once

namespace state {
    //inline const int STOPWAIT_SIGNAL = SIGRTMIN + 7;

    enum class Child {
        running,        //child process is still running, should be acted upon by debugger
        faulting,       //child process is still running, execution is paused due to a fault (ex: SIGSEGV)
        detach,         //child process is still running, user wants to exit debugger process (gracefully)
        force_detach,   //child process is on the verge of abnormal termination, exit debugger (forcefully)
        finish,         //End of main() in child process, terminating debugger process
        kill,           //Child process should be terminated, terminate both debugger and child
        crashed,        //Child process has terminated abnormally, exit debugger process
        terminated      //Child process has terminated normally, exit debugger process
    };

    constexpr bool isExecuting(Child state) {
        return state == Child::running || state == Child::faulting;
    }

    constexpr bool isTerminating(Child state) {
        return state == Child::detach || state == Child::force_detach
            || state == Child::finish || state == Child::kill;
    }

    constexpr bool isTerminated(Child state) {
        return state == Child::crashed || state == Child::terminated;
    }
}