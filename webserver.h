#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

// 最大文件描述符
const int MAX_FD = 65536;
// 最大事件数
const int MAX_EVENT_NUMBER = 10000;
// 最小超时单位
const int TIMESLOT = 5;


class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port, std::string user, std::string passWord, std::string databaseName,
              int log_write, int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model,int db_port = 3306);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclientdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    /********************基础信息******************/
    // 服务器端口
    int m_port;
    // 资源文件根目录
    char *m_root;
    // 日志类型(异步/同步)
    int m_log_write;
    // 是否启动日志
    int m_close_log;
    // 事件处理模式 Reactor/Proactor
    int m_actormodel;
    /********************基础信息******************/

    /********************网络信息******************/
    // 管道套接字，用于定时器设计的统一事件源
    int m_pipefd[2];
    // epoll对象
    int m_epollfd;
    // http连接对象指针，指向http连接对象数组
    http_conn *users;
    /********************网络信息******************/

    /********************数据库相关******************/
    connection_pool *m_connPool;
    // 登陆数据库的用户名
    std::string m_user;
    // 登陆数据库的密码
    std::string m_passWord;
    // 使用数据库名
    std::string m_databaseName;
    // 数据库连接池中数据库连接数量
    int m_sql_num;
    // 数据库服务器端口
    int m_db_port;
    /********************数据库相关******************/

    /********************线程池相关******************/
    threadpool<http_conn> *m_pool;
    // 线程池工作线程数量
    int m_thread_num;
    /********************线程池相关******************/

    /********************epoll_event相关******************/
    // IO复用系统，用于存储就绪的文件描述符信息
    epoll_event events[MAX_EVENT_NUMBER];
    // 用于监听客户端连接请求的套接字
    int m_listenfd;
    // 是否优雅下线
    int m_OPT_LINGER;
    // epoll的（事件触发模式）工作模式  电平ET/边沿LT
    int m_TRIGMode;
    // 监听套接字的事件触发模式
    int m_LISTENTrigmode;
    // 连接套接字的事件触发模式
    int m_CONNTrigmode;
    /********************epoll_event相关******************/

    /********************定时器相关******************/
    // 定时器连接资源指针，指向定时器连接资源数组
    client_data *users_timer;
    // 定时器中的工具类
    Utils utils;
    /********************定时器相关******************/
};