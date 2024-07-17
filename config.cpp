#include "config.h"
#include <fstream>
#include <jsoncpp/json/json.h>  // JsonCpp库

Config::Config()
{
    // 端口号,默认9006
    PORT = 9006;

    // 日志写入方式，默认同步
    LOGWrite = 0;

    // 触发组合模式,默认listenfd LT + connfd LT
    TRIGMode = 0;

    // listenfd（监听套接字事件）触发模式，默认LT
    LISTENTrigmode = 0;

    // connfd（通信套接字事件）触发模式，默认LT
    CONNTrigmode = 0;

    // 优雅关闭http链接，默认不使用
    OPT_LINGER = 0;

    // 数据库连接池中数据库连接的数量,默认8
    sql_num = 8;

    // 线程池内的线程数量,默认8
    thread_num = 8;

    // 关闭日志,默认不关闭
    close_log = 0;

    // 并发模型(事件处理模式),默认是proactor
    actor_model = 0;

    // 数据库的服务器端口,默认为3306
    db_Port = 3306;
}


/*
 * @func: 解析JSON配置文件
 */
bool Config::parseJsonFile()
{
    // 文件刘读取JSON文件
    std::ifstream ifs("/home/zxz/Proj/C_C++/WebServer/TinyWebServerBymyself/dbconf.json");
    // Json::Reader 对象用于解析JSON数据
    Json::Reader rd;
    // Json::Value 对象用于存储解析后的JSON数据结构
    Json::Value root;
    // parse方法将输入文件流 ifs 中的内容解析到 root 对象中
    rd.parse(ifs,root);
    if(root.isObject())
    {
        user = root["userName"].asString();
        password = root["password"].asString();
        databasename = root["dbName"].asString();
        db_Port = root["db_port"].asInt();
        return true;
    }
    return false;
}

/*
 * @func: 解析命令行终端的选项输入
 */
void Config::parse_arg(int argc, char*argv[]){
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    // getopt函数用于解析命令行选项（短选项）
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
            case 'p':
            {
                // 服务器程序监听端口
                PORT = atoi(optarg);
                break;
            }
            case 'l':
            {
                // 日志写入方式（异步/同步）
                LOGWrite = atoi(optarg);
                break;
            }
            case 'm':
            {
                // 触发组合模式
                TRIGMode = atoi(optarg);
                break;
            }
            case 'o':
            {
                // 优雅关闭http链接
                OPT_LINGER = atoi(optarg);
                break;
            }
            case 's':
            {
                // 数据库连接池中数据库连接数量
                sql_num = atoi(optarg);
                break;
            }
            case 't':
            {
                // 线程池中工作线程数量
                thread_num = atoi(optarg);
                break;
            }
            case 'c':
            {
                // 是否关闭日志
                close_log = atoi(optarg);
                break;
            }
            case 'a':
            {
                // 并发模型(事件处理模式)
                actor_model = atoi(optarg);
                break;
            }
            default:
                break;
        }
    }
}