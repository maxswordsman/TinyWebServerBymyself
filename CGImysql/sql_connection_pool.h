#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <list>
#include <mysql/mysql.h>
#include "../lock/locker.h"
#include "../log/log.h"


class connection_pool
{
public:
    // 获取唯一的数据库连接池的静态接口
    static connection_pool* GetInstance();
    // 从池中获取空闲连接
    MYSQL *GetConnection();
    // 释放连接,将操作完毕的连接归还至连接队列中
    bool ReleaseConnection(MYSQL *conn);
    // 获取池中空闲连接数
    int GetFreeConn();
    // 销毁所有连接
    void DestroyPool();
    // 初始化连接池相关属性
    void init(std::string url, std::string User, std::string PassWord,
              std::string DataBaseName, int Port, int MaxConn, int close_log);

private:
    // 构造函数私有化
    connection_pool();
    ~connection_pool();
    // 复制构造函数删除
    connection_pool(const connection_pool &) = delete;
    // 复制运算符删除
    connection_pool& operator=(const connection_pool &) = delete;

    // 连接池中最大连接数
    int m_MaxConn;
    // 当前已使用的连接数
    int m_CurConn;
    // 当前空闲的连接数
    int m_FreeConn;
    // 互斥锁，访问共享资源，连接队列，保证线程安全
    locker lock;
    // 信号量
    sem reserve;
    // 连接队列，使用list STL实现
    std::list<MYSQL *> connList;

public:
    // 数据库服务器ip
    std::string m_url;
    // 数据库端口号 默认为3306
    std::string m_Port;
    // 登陆数据库用户名
    std::string m_User;
    // 登陆数据库密码
    std::string m_PassWord;
    // 使用数据库名
    std::string m_DatabaseName;
    // 日志开关
    int m_close_log;
};


// RAII机制，归还数据库连接
class connectionRAII
{
public:
    connectionRAII(MYSQL **con,connection_pool *connectionPool);
    ~connectionRAII();
private:
    MYSQL  *conRAII;
    connection_pool *poolRAII;
};
