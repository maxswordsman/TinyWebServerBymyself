#include "sql_connection_pool.h"

using namespace std;


/*
 * func: 数据池私有构造函数
 */
connection_pool::connection_pool()
{
    m_CurConn = 0;
    m_FreeConn = 0;
}


/*
 * func: 获取唯一连接池实例的静态接口
 * note: 使用局部静态变量实现懒汉式单例模式
 *       C+11 局部静态变量是线程安全的
 */
connection_pool *connection_pool::GetInstance()
{
    // 局部静态变量实现单例模式
    static connection_pool connPool;
    return &connPool;
}


/*
 * func: 初始化连接池相关属性
 * parameter:
 *          url:数据库服务器ip(若数据库服务器与webServer服务器位于一台服务器上,即主机ip)
 *          User:数据库服务器用户名
 *          PassWord:数据库服务器密码
 *          DBName:数据库服务器中数据库名称
 *          Port:数据库服务器端口号,默认为3306
 *          MaxConn:池中最大连接数
 *          close_log:是否关闭日志标志
 */
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    m_close_log = close_log;

    // 初始创建一定数量的数据库连接，放入连接队列中
    for (int i = 0; i < MaxConn; i++)
    {
        MYSQL *con = NULL;
        // 初始化MYSQL对象
        con = mysql_init(con);

        if (con == NULL)
        {
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        // 连接MYSQL服务器
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

        if (con == NULL)
        {
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        // 数据库连接对象，装载至队列中
        connList.push_back(con);
        // 空闲连接数量+1
        ++m_FreeConn;
    }

    // 初始化信号量，信号量数量为空闲连接数量
    reserve = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
}


/*
 * func: 获取数据库连接
 * note: 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
 *       若此时连接队列中无可用请求，使用信号量对请求线程进行阻塞
 */
MYSQL *connection_pool::GetConnection()
{
    MYSQL *con = NULL;

    if (0 == connList.size())
        return NULL;

    // 等待信号量，若队列中空闲信号量为0，则阻塞线程
    // 若信号量不为0，则信号量-1，之后的操作，从队列中取出一个空闲的连接
    reserve.wait();

    lock.lock();

    // 取出队列首部的连接
    con = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();
    return con;
}



/*
 * func: 释放连接,将操作完毕的连接归还至连接队列中
 */
bool connection_pool::ReleaseConnection(MYSQL *con)
{
    if (NULL == con)
        return false;

    lock.lock();

    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();

    // 通知阻塞在连接队列的请求数据库连接的线程
    reserve.post();
    return true;
}

/*
 * func: 销毁所有连接
 */
void connection_pool::DestroyPool()
{

    lock.lock();
    if (connList.size() > 0)
    {
        list<MYSQL *>::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it)
        {
            MYSQL *con = *it;
            // 关闭连接
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}


/*
 * func: 获取池中空闲连接数
 */
int connection_pool::GetFreeConn()
{
    return this->m_FreeConn;
}


/*
 * func: 连接池的析构函数，销毁所有连接
 */
connection_pool::~connection_pool()
{
    DestroyPool();
}


/*
 * func: 通过RAII机制，控制连接，
 *       当数据库连接操作完成后，将连接归还至连接队列
 */
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
    *SQL = connPool->GetConnection();
    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(conRAII);
}