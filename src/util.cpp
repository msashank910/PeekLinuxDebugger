#include "../include/util.h"

#include <string>
#include <string_view>
#include <vector>
#include <iostream>
#include <sstream>
#include <string_view>
#include <charconv>
#include <optional>
#include <memory>       //for unique pointers
#include <cxxabi.h>     //for demangling - part of libstdc++ ABI in linux (for gcc and clang)


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
        std::string temp = "";
    
        while(getline(ss, temp, delimiter)) {
            if(!temp.empty()) args.push_back(temp);
        }
    
        return args;
    }

    //Only converts if string is completely valid and num can hold value
    bool validHexStol(uint64_t& num, std::string_view addr) {
        uint64_t buffer;
        auto[ptr, ec] = std::from_chars(addr.data(), addr.data() + addr.size(), buffer, 16);
        if(ptr == addr.end() && ec == std::errc()) {
            num = buffer;
            return true;
        }
        return false;
    }

    //Only converts if string is completely valid and num can hold value
    bool validDecStol(uint64_t& num, std::string_view dec) {
        uint64_t buffer;
        auto[ptr, ec] = std::from_chars(dec.data(), dec.data() + dec.size(), buffer, 10);
        if(ptr == dec.end() && ec == std::errc()) {
            num = buffer;
            return true;
        }
        return false;
    }


    bool hasWhiteSpace(const std::string_view s) {
        return s.find_first_of(" \t") != std::string_view::npos;
    }


    /*
        Handle demangling of symbols below. The demangling process uses the libstdc++ ABI library in linux.
        The demangling doesn't fully result in a readable string - so I take steps to make common symbols more 
        readable. Below is my naive approach. It is not exhaustive but allows support for additional support 
        in the future.
    */


    std::optional<std::string> demangleSymbol(const std::string& symbol) {
        int status;
        std::unique_ptr<char, decltype(&free)> demangled(abi::__cxa_demangle
                (symbol.data(), nullptr, nullptr, &status), free);

        if(!demangled || status )   //implicitly casted to boolean (0 means false)
            return std::nullopt;

        //return demangled.get();
        std::string readable = demangledToReadable(demangled.get());
        return readable;
    }


    static constexpr auto verboseSTLList = std::to_array<std::pair<std::string_view, std::string_view>> ({
        {"std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string"},
        {"std::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string"},
        {"std::basic_ostream<char, std::char_traits<char> >", "std::ostream"},
        {"std::basic_istream<char, std::char_traits<char> >", "std::istream"},
        {"std::basic_iostream<char, std::char_traits<char> >", "std::iostream"},
        {"std::allocator<std::string>", ""},
        // {"std::allocator<char>", ""},
        // {"std::allocator<int>", ""},
        // {"std::allocator<long int>", ""},
        // {"std::allocator<unsigned long int>", ""}
    });

    static constexpr auto verbosePrimativeList = std::to_array<std::pair<std::string_view, std::string_view>> ({
        {"long unsigned int", "uint64_t"},
        {"unsigned int", "uint32_t"},
        {"unsigned long", "uint64_t"},
        {"long int", "int64_t"},
       // {"int", "int32_t"},
        {"char const*", "const char*"}

    });
    
    static constexpr auto deleteList = std::to_array<std::string_view> ({
        {"std::allocator"},
        {"std::less"},
        {"std::equal_to"},
        {"std::hash"}

    });
    static constexpr auto containerParameterList = std::to_array<std::pair<std::string_view, int>> ({
        {"std::vector", 1},
        {"std::unordered_map", 2},
        {"std::map", 2},
        {"std::stack", 1},
        {"std::priority_queue", 1},
        {"std::array", 1}

    });

/*
    Parameter: A const reference to an already demangled string.
    Return: A std::string that has been simplified to be more readable.

    A demangled symbol can still contain elements and garbage that needs to be filtered out. While I do not 
    have an exhaustive list of all possible elements to filter out, I will focus on the most common types of 
    such elements that can be encountered (more can be added over time, there is support for this). Filtering 
    takes place in 4 steps:

    1) Replace instances of common STL elements/objects like iostream and string.
    2) Replace common verbose primitive types to concise types.
    3) Delete elements that do not need to be in a human-readable output (allocate, hash, less).
    4) Simplify common container types in std namespace (vector, map, etc.).

*/
    std::string demangledToReadable(const std::string& demangled) {
        std::string readable = demangled;
        //DEBUG STATEMENT
        //return readable 

        for(const auto&[instance, replace] : verboseSTLList) {
            auto index = readable.find(instance);
            while(index != std::string::npos) {
                readable.replace(index, instance.length(), std::string(replace));
                index += replace.length();
                index = readable.find(instance, index);
            }
        }
        for(const auto&[instance, replace] : verbosePrimativeList) {
            auto index = readable.find(instance);
            while(index != std::string::npos) {
                readable.replace(index, instance.length(), std::string(replace));
                index += replace.length();
                index = readable.find(instance, index);
            }
        }
        
        for(const auto& instance : deleteList) {
            auto index = readable.find(instance);
            while(index != std::string::npos) {     //guaranteed not to be infinite loop due to deletions
                auto posAfterEndOfIndex = index + instance.length();
                int openAngleCount = (posAfterEndOfIndex < (readable.length()) 
                    && readable[posAfterEndOfIndex] == '<' ? 1 : 0);
                int deletionLength = instance.length() + openAngleCount;

                while(openAngleCount >= 1) {
                    auto openAnglePos = readable.find_first_of('<', index + deletionLength);
                    auto closeAnglePos = readable.find_first_of('>', index + deletionLength);
                    /*
                        deletionLength represents the number of characters to be deleted from the specified 
                        index. By taking the difference of two indecies (close/open and index) and adding 
                        one to the result, you can retrieve the number of characters in the range [start, end].
                    */
                    if(closeAnglePos == std::string::npos) {    //should NEVER get here
                        throw std::logic_error("[fatal] In util::demangledToReadable() - "
                            "no template arguments found for template function!");
                    }
                    else if(openAnglePos == std::string::npos) {
                        deletionLength = closeAnglePos - index + 1;
                        --openAngleCount; //should be zero now
                        //DEBUG STATEMENT
                        //std::cout << "template args should be finished now!\n";
                    }
                    else if(openAnglePos < closeAnglePos) { //sub-template args need to be dealt with 
                        ++openAngleCount;
                        deletionLength = openAnglePos - index + 1;
                    }
                    else {  //finished a set of template args
                        --openAngleCount;
                        deletionLength = closeAnglePos - index + 1;
                    }
                }
                readable.replace(index, deletionLength, "");
                index = readable.find(instance);        //due to deletion, no need to offset index
            }
        }
        
        for(const auto&[instance, templateArgCount] : containerParameterList) { //one-indexed arg-count
            auto index = readable.find(instance);   //increment index to prevent infinite loop
            auto openAnglePos = readable.find_first_of('<', index + instance.length());

            while(index != std::string::npos && openAnglePos != std::string::npos) {     
                int openAngleCount = 1;
                int currArg = 1;
                int startDeletion = -1;     //check if still negative later before starting deletion
                /*
                First, we need to check if a comma exists. If a container is already in form: 
                container<template>, there is no point in iterating through the characters. That is why 
                startDeletion is initialized to a negative value here. 
                The for-loop iterates over each char in the string past the position of the initial 
                open angle. The for-loop is split up into 2 main parts -> switch, and if-block.
                Switch steps:
                    1) if char is '<', a sub-template arg has been introduced, increment openAngleCount.
                    2) if char is '>', a template arg has concluded, Decrement openAngleCount. Go to if-block.
                    3) if char is a ',', go directly to if-block.
                    4) if char is anything else, continue iterating over the string.
                
                If-block steps:
                    1) if angleCount is below 1, terminate for-loop, continue to next instance.
                    2) if angleCount is above 1, continue iterating over the string.
                    3) if angleCount is 1 AND the current argCount is less than the total expected args:
                        -> increment the current argCount and move i to the next instance of '>' or '<'.
                    4) if angleCount is 1 AND the current argCount is equal to total expected args:
                        -> set startDeletion position to the current index and break loop.
                */

                for(auto i = openAnglePos + 1; i < readable.length(); i++) {
                    switch(readable[i]) {
                        case '<':
                            ++openAngleCount;
                            continue;
                        case '>':
                            --openAngleCount;   //break here to exit loop if template params have completed
                            break;
                        case ',':
                            break;
                        default:
                            continue;
                    }
                    if(openAngleCount < 1) {    //exits loop when template params are already optimal
                        break;
                    }
                    else if(openAngleCount > 1 || readable[i] != ',') continue;
                    else if(openAngleCount == 1 && templateArgCount > currArg) {
                        //i is incremented to the next position of an openAngleBracket or the next comma
                        //This checks for 1) sub-template args and 2) handles primatives/objects
                        i = readable.find_first_of("<,", i);  
                        ++currArg;
                        continue;
                    }
                    /*
                    Reaching this point means three things are true: 
                        1) Index is at a comma --> seperation between template args
                        2) openAngleCount = 1 --> not in a sub-template arg
                        3) Necessary template args have been iterated through
                    
                    This means we can begin deletion length below.
                    */
                    startDeletion = i;
                    break;
                }
                /*
                    startDeletion can equal -1 in one of two ways.
                        1) There are no commas to be found in the string.
                        2) The template parameters are already optimal.
                    Both of these means that we can move onto the next instance.
                */
                if(startDeletion == -1) {   //look for more instances
                    index = readable.find(instance, instance.length() + index);
                    openAnglePos = readable.find_first_of('<', index);
                    continue;
                    // throw std::logic_error("[fatal] In util::demangledToReadable() - "
                    //         "Could not resolve template arguments in template function!");
                }

                int deletionLength = 0;

                //begin iterating, looking for useless template args
                while(openAngleCount >= 1) {
                    auto openAnglePos = readable.find_first_of('<', startDeletion + deletionLength);
                    auto closeAnglePos = readable.find_first_of('>', startDeletion + deletionLength);
                    
                    if(closeAnglePos == std::string::npos) {    //should NEVER get here
                        throw std::logic_error("[fatal] In util::demangledToReadable() - "
                            "no template arguments found for template function!");
                    }
                    else if(openAnglePos == std::string::npos) {    //can simplify this later
                        deletionLength = closeAnglePos - startDeletion + 1;
                        --openAngleCount; //should be zero now
                        //DEBUG STATEMENT
                        //std::cout << "template args should be finished now!\n";
                    }
                    else if(openAnglePos < closeAnglePos) { //sub-template args need to be dealt with 
                        ++openAngleCount;
                        deletionLength = openAnglePos - startDeletion + 1;
                    }
                    else {  //finished a set of template args
                        --openAngleCount;
                        deletionLength = closeAnglePos - startDeletion + 1;
                    }
                }
                --deletionLength;   //account for final ">" that shouldn't be deleted
                readable.replace(startDeletion, deletionLength, "");
                index = readable.find(instance, instance.length() + index);
                openAnglePos = readable.find_first_of('<', index);      //must offset index
            } 
        }
        return readable;
    }

}
