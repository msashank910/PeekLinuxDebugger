#pragma once


#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>

//TODO - split into structs for different config groups (debuggerconfig, etc.)
//Then make config public mem vars debugger, symbol, memory
class Config {

    //possible handling of other debugger instance flags


    //possible handling of other symbolmap configurations

    //handling of certain memorymap configs (how often you reload memory chunks etc..)

    


public:

    struct DebugConfig {
        bool verbose_ = true;
        uint8_t context_ = 3;
        DebugConfig();
    };

    /*
        Most of this is caching related things. Will this cause bloat? Probably so with the memory overhead
        from storing all these pointers and iterators. But do I care? Not really, I enjoyed the caching 
        process. If you're reading this, I did it because I LIKED IT.
    */
    struct SymbolConfig {
    // size_t symbolListSize_;
        size_t symbolCacheSize_ = 100;
        // bool strictSymbolMatch_ = false;
        uint8_t minCachedStringLength_ = 3; 
        SymbolConfig();
        std::string touchKey(const std::string&);
        std::vector<std::string> configureCache();
    
    private:
        std::list<std::string> lruList_;
        std::unordered_map<std::string_view, std::list<std::string>::iterator> lruMap_;
        std::string lru();
    };


    DebugConfig debugger_;
    SymbolConfig symbol_; 
    
    

    Config();


};