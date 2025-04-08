#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <string_view>
#include <optional>
#include <utility>


namespace util {
    std::string_view stripAddrPrefix(const std::string_view& s);  //change function later
    
    inline bool isPrefix(const std::string_view prefix, const std::string_view word) {
        return word.starts_with(prefix);
    }

    bool validHexStol(uint64_t& num, std::string_view addr);
    bool validDecStol(uint64_t& num, std::string_view dec);
    
    std::vector<std::string> splitLine(const std::string &line, char delimiter);
    bool hasWhiteSpace(const std::string_view s);

    std::optional<std::string> demangleSymbol(const std::string& symbol, bool makeReadable = true);
}