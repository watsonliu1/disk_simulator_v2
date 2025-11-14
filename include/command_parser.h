#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <string>
#include <vector>

// 命令类型枚举：定义支持的所有命令，便于后续switch-case处理
enum class CommandType 
{
    LS,         // 列出文件
    CAT,        // 查看文件内容
    RM,         // 删除文件
    COPY,       // 复制文件
    WRITE,      // 写入文件
    TOUCH,      // 创建空文件
    EXIT,       // 退出程序
    EMPTY,      // 空输入（仅回车）
    UNKNOWN     // 未知命令
};

/**
 * @brief 解析命令字符串，返回命令类型和参数
 */
CommandType parse_command(const std::string& input, std::vector<std::string>& args);

#endif // COMMAND_PARSER_H