#include "config.h"
#include <iostream>

int main(int argc, char *argv[])
{

    Config config;
    // 读取数据库配置文件dbconf.json,修改数据库信息
    config.parseJsonFile();
    // 命令行解析
    config.parse_arg(argc,argv);
    std::cout << config.user <<std::endl;
    std::cout << config.password << std::endl;
    std::cout << config.databasename << std::endl;
    std::cout << config.db_Port << std::endl;

    WebServer server;

    // 初始化服务器相关变量
    server.init(config.PORT, config.user, config.password, config.databasename,
                config.LOGWrite, config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num,
                config.close_log, config.actor_model, config.db_Port);

    // 初始化日志系统
    server.log_write();

    // 初始化数据库连接池
    server.sql_pool();

    // 初始化线程池
    server.thread_pool();

    // 设置epoll实例上挂载的监听与通信的套接字文件描述符事件触发模式
    server.trig_mode();

    // 服务器用于监听的套接字ip等属性的设置
    // IO复用系统epoll实例的创建与设置，将用于监听的套接字挂到epoll实例上，进行监测
    // 定时器系统用于统一事件源的管道套接字创建设置
    server.eventListen();

    // 运行
    server.eventLoop();

    return 0;
}
