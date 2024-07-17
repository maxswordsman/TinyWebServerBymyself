#pragma once
#include "webserver.h"


class Config
{
public:
    Config();
    ~Config(){};

    // 解析终端命令传入的参数
    void parse_arg(int argc, char*argv[]);

    // 解析JSON数据库配置文件
    bool parseJsonFile();

    // 端口号
    int PORT;

    // 日志写入方式（异步/同步）
    int LOGWrite;

    // 触发组合模式
    int TRIGMode;

    // listenfd事件触发模式（监听套接字）
    int LISTENTrigmode;

    // connfd事件触发模式（通信套接字）
    int CONNTrigmode;

    // 是否优雅关闭链接（套接字关闭时，是否等待接收/发送的数据完成）
    int OPT_LINGER;

    // 数据库连接池中数据库连接的数量
    int sql_num;

    // 线程池内的线程数量
    int thread_num;

    // 是否关闭日志
    int close_log;

    // 并发模型选择（事件处理模式）
    int actor_model;

    // 数据库登陆用户名
    std::string user;
    // 数据库登陆密码
    std::string password;
    // 数据库名字
    std::string databasename;
    // 数据库服务器端口
    int db_Port;
};