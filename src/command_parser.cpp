#include "../include/command_parser.h"
#include <sstream>
#include <algorithm>

CommandType parse_command(const std::string& input, std::vector<std::string>& args) {
    args.clear();
    std::istringstream iss(input);
    std::string token;

    if (!(iss >> token)) {
        return CommandType::EMPTY;
    }

    std::transform(token.begin(), token.end(), token.begin(), ::tolower);

    std::string arg;
    while (iss >> arg) {
        args.push_back(arg);
    }

    if (token == "ls") {
        return CommandType::LS;
    } else if (token == "cat") {
        return CommandType::CAT;
    } else if (token == "rm") {
        return CommandType::RM;
    } else if (token == "copy") {
        return CommandType::COPY;
    } else if (token == "write") {  
        return CommandType::WRITE;
    } else if (token == "exit") {
        return CommandType::EXIT;
    } else if (token == "touch") {
        return CommandType::TOUCH;
    } else {
        return CommandType::UNKNOWN;
    }
}



