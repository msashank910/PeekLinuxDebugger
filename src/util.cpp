#include "../include/util.h"

#include <string>
#include <string_view>
#include <vector>
#include <iostream>
#include <sstream>
#include <string_view>
#include <charconv>

//Helpers 

namespace util {
    std::string_view stripAddrPrefix(const std::string_view& s) {          
        auto res = (!s.empty() && s[0] == '*' ? s.substr(1) : s);   //removes * for relative addresses

        if(res.length() > 2 && res[0] == '0' && (res[1] == 'x' || res[1] == 'X')) { //strips 0x or 0X
            return res.substr(2);
        }
        return res;
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
