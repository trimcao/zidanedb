#ifndef ZIDANEDB_DATABASE_H
#define ZIDANEDB_DATABASE_H

#include <string>
#include <unordered_map>
#include <filesystem>
namespace zidanedb {

class Database {
private:
    std::string filename; // for persistence
    std::unordered_map<std::string, std::string> data_;

public:
    explicit Database(std::filesystem::path path);
    std::string get(std::string key);
    int put(std::string key, std::string val);
    int del(std::string key);
};

}

#endif
