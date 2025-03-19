#include "../include/util.h"

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <string_view>
#include <charconv>

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


    bool validStol(uint64_t& num, std::string_view addr) {
        return (std::from_chars(addr.data(), addr.data() + addr.size(), num, 16).ec == std::errc{});
        //auto err = std::from_chars(addr.data(), addr.data() + addr.size(), num, 16);
        //if(err.ec == std::errc()) return true;
    }
}
