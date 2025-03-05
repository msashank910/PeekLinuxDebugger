#include "../include/util.h"

#include <string>
#include <vector>
#include <iostream>
#include <sstream>

//Helpers 

namespace util {
    std::string strip0x(const std::string& s) {          //maybe change later
        if(!s.empty() && s.length() > 2 && s[0] == '0' && s[1] == 'x') {
            return std::string(s.begin() + 2, s.end());
        }
        return s;
    }
    //isPrefix is declined as inline in util.h
    
    std::vector<std::string> splitLine(const std::string &line, char delimiter) {    
        std::vector<std::string> args;
        std::stringstream ss(line);
        std::string temp;
    
        while(getline(ss, temp, delimiter)) {
            args.push_back(temp);
        }
    
        return args;
    }
}
