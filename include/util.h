#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <string_view>
#include <optional>
#include <utility>
//#include <unordered_map>


namespace util {
    std::string_view stripAddrPrefix(const std::string_view& s);  //change function later
    
    inline bool isPrefix(const std::string_view prefix, const std::string_view word) {
        return word.starts_with(prefix);
    }

    bool validHexStol(uint64_t& num, std::string_view addr);
    bool validDecStol(uint64_t& num, std::string_view dec);
    
    std::vector<std::string> splitLine(const std::string &line, char delimiter);
    bool hasWhiteSpace(const std::string_view s);

    std::optional<std::string> demangleSymbol(const std::string& symbol);

    std::string demangledToReadable(const std::string& demangled);
    // extern constexpr std::vector<std::pair<std::string, std::string>> verboseSTLList;
    // extern const std::vector<std::pair<std::string, std::string>> verbosePrimativeList;
    // extern const std::vector<std::pair<std::string, std::string>> verboseContainerList;
}