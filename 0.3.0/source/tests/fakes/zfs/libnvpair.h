#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
struct nvlist_t {
    std::map<std::string,std::string> strings;
    std::map<std::string,std::uint64_t> integers;
    std::map<std::string,nvlist_t*> lists;
    std::map<std::string,std::vector<nvlist_t*>> arrays;
};
enum boolean_t { B_FALSE, B_TRUE };
extern "C" {
int nvlist_lookup_string(const nvlist_t*, const char*, const char**);
int nvlist_lookup_uint64(const nvlist_t*, const char*, std::uint64_t*);
int nvlist_lookup_nvlist(nvlist_t*, const char*, nvlist_t**);
int nvlist_lookup_nvlist_array(nvlist_t*, const char*, nvlist_t***, unsigned int*);
boolean_t nvlist_exists(const nvlist_t*, const char*);
}
