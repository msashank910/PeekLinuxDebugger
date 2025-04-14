#include "../include/config.h"

#include <cstdint>
#include <list>
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>


// Config::Config() : verbose_(true), context_(3), symbolListSize_(1000), symbolCacheSize_(100),
//     strictSymbolMatch_(false), minCachedStringLength_(3) {
//     //TODO - Flesh out constructor
// }

Config::Config() : debugger_(), symbol_() {}
Config::DebugConfig::DebugConfig() = default;
Config::SymbolConfig::SymbolConfig() = default;

std::string Config::SymbolConfig::touchKey(const std::string& key) {
    auto lifoKey = lruMap_.find(key);
    if(lifoKey == lruMap_.end()) {
        lruList_.push_back(key);
        std::string_view keyView = lruList_.back();
        lruMap_[keyView] = --lruList_.end();   //last key in list
        return lru();
    }

    lruList_.splice(lruList_.end(), lruList_, lifoKey->second);
    return "";
}


std::string Config::SymbolConfig::lru() {
    std::string removed;
    if(lruList_.size() > symbolCacheSize_) {
        std::string_view removalKey = lruList_.front();
        removed = lruList_.front();      //RVO will handle this redundancy
        lruMap_.erase(removalKey);
        lruList_.pop_front();
    }
    return removed;
}

/*
    This function collects all the keys that needed to be deleted SymbolMap::nonStrictSymbolCache. It first 
    loops over all short strings in the map. Then, in a seperate pass, it loops over the collected strings 
    to delete one-by-one from the lru list and map. Lastly, the function loops lru() to evict least recently 
    used strings from the lru list/map. Every deletion key from the short string and lru() passes are 
    added to a vector "deletion", that is returned to the caller.
*/
std::vector<std::string> Config::SymbolConfig::configureCache() {
    std::vector<std::string> deletion;
    for(const auto& itr : lruMap_ ) {
        if(itr.first.length() < static_cast<size_t>(minCachedStringLength_)) {
            deletion.push_back(*itr.second); //references key in list
        }
    }

    for(const auto& sv : deletion) {
        auto found = lruMap_.find(sv);
        if(found == lruMap_.end()) continue;

        auto lruLocation = found->second;
        lruMap_.erase(sv);
        lruList_.erase(lruLocation);
    }

    std::string del;
    while(!(del = lru()).empty()) {
        deletion.push_back(del);
    }
    return deletion;
}

