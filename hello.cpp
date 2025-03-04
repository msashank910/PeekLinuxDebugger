#include <iostream>

int main() {

    int first = 10;
    int second = 26;
    int third = first + second;
    
    //using cerr over cout bc cout doesn't write out unless \n (or endl which flushes buffer)
    std::cerr << first << " + " << second << " = " << third << "\n";   

    return 0;
}