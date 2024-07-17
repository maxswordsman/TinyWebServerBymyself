#pragma once

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include <iostream>

/*
 * 线程池类，将其定义为模板类是为了代码复用，模板参数T是任务类
 * 注意;
 *      模板类以及成员函数不是 不是类以及函数定义，因此无法单独放到一个cpp文件下，编译u
 *      需要将模板类，以及模板成员函数均放到一个头文件中
 */
template <typename  T>
class threadpool
{
public:
    // thread_number是线程池中线程的数量
    // max_requests是请求队列中最多允许的、等待处理的请求的数量(任务队列容量)
    // connPool是数据库连接池指针
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    // 向任务队列中插入任务
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    // 线程池工作线程的任务函数，从任务队列中取出任务并且执行
    // static 修饰 该函数是类级别的，可以在没有创建类实例的情况使用
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;          // 线程池中的线程数
    int m_max_requests;           // 请求队列中允许的最大请求数(任务队列的容量)
    pthread_t *m_threads;         // 描述线程池的数组，其大小为m_thread_number (用于存储线程池工作线程的线程ID)
    std::list<T *> m_workqueue;   // 请求队列（任务队列）-- list 双向链表
    locker m_queuelocker;         // 保护请求队列的互斥锁
    sem m_queuestat;              // 信号量类对象，是否有任务需要处理
    connection_pool *m_connPool;  // 数据库
    int m_actor_model;            // 模型切换（这个切换是指Reactor/Proactor）
};


/*
 * @func: 线程池构造函数--线程池的创建
 * @note: 使用成员列表初始化，对成员变量进行初始化
 * @param: actor_model 事件处理模式 1表示Reactor模式  0表示Proactor模式
 * @param: connection_pool 数据库连接池对象地址
 * @param: thread_number 线程池中工作线程数量
 * @param: max_requests 请求队列大小
 */
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool,
                           int thread_number, int max_requests) :
                           m_actor_model(actor_model),m_thread_number(thread_number),
                           m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        // 参数不正确，抛异常
        throw std::exception();

    // 为工作线程数组分配内存
    m_threads = new pthread_t[m_thread_number];     //pthread_t是长整型
    if (!m_threads)
        throw std::exception();

    for (int i = 0; i < thread_number; ++i)
    {
        // 函数原型中的第三个参数，为函数指针，指向处理线程函数的地址。
        // 若线程函数为类成员函数，
        // 则this指针会作为默认的参数被传进函数中，从而和线程函数参数(void*)不能匹配，不能通过编译
        // 静态成员函数就没有这个问题，因为里面没有this指针
        // this表示的为线程池对象
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        // 对创建的工作线程进行线程分离，之后不需要主动对工作线程进行线程资源回收
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}


/*
 * @func: 析构函数
 */
template <typename T>
threadpool<T> ::~threadpool()
{
    delete[] m_threads;
}


/*
 * @func: Reactor模式下的请求入队
 * @param: request入队任务---在该项目中request是一个http的连接请求
 * @param: state任务类型 0表示读事件 1表示写事件
 */
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    // 操作请求（任务）队列时，需要加锁，因为这是线程池的共享资源
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        // 任务队列满，无法继续添加任务
        return false;
    }
    //读写事件
    request->m_state = state;
    // 任务队列，队尾插入任务
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    // 信号量+1，唤醒一个阻塞在请求队列的工作线程
    m_queuestat.post();
    return true;
}


/*
 * @func: Proactor模式下的请求入队
 * @note: 同步IO模拟proactor模式下
 *       主线程负责epoll实例中的文件描述符监听，以及IO的读写操作；而工作线程仅仅负责业务处理逻辑
 */
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        // 任务队列满
        return false;
    }
    // 任务队列，队尾插入任务
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    // 信号量+1，唤醒一个阻塞在请求队列的工作线程
    m_queuestat.post();
    return true;
}


/*
 * @func: 工作线程，任务处理函数，在函数体中，运行私有成员函数run方法
 * @param: arg 线程池实例对象的地址
 */
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    // 调用时 *arg是this！
    // 所以该操作其实是获取threadpool对象地址
    threadpool *pool = (threadpool *)arg;
    // 线程池中每一个线程创建时都会调用run()
    // 在阻塞队列中取出http对象并且处理任务
    pool->run();
    return pool;
}


/*
 * @func: 工作线程从任务队列中取出任务，并且执行
 */
template <typename T>
void threadpool<T>::run()
{
    while(true)
    {
        // 申请信号量，若信号量值为0，则阻塞
        m_queuestat.wait();
        // 任务队列为线程池共享资源，需要加锁，实现线程同步
        m_queuelocker.lock();
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        // 从任务队列中，取任务
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request){
            continue;
        }

        // Reactor 模式
        // 主线程仅负责，文件描述符的监控。IO数据读写以及业务处理均为工作子线程负责
        // 进行事件处理模式的选择判断
        if(1 == m_actor_model)
        {
            // IO事件类型：0为读事件
            // request为一个http连接请求对象
            if(0 == request->m_state)
            {
                // 执行IO数据的读取，从http连接的通信套接字的读缓冲区
                // 将数据读取至m_read_buf中
                if(request->read_once())
                {
                    // 将improv标志位设置为1，表示数据正在处理
                    request->improv = 1;
                    // 使用RAII机制管理，该http请求的数据库连接请求
                    connectionRAII mysqlcon(&request->mysql,m_connPool);
                    // http连接请求对象调用process函数，对m_read_buf中的数据进行解析
                    request->process();
                }
                else
                {
                    // IO数据读取失败
                    request->improv = 1;
                    // 将关闭连接的标志位设置为1
                    request->timer_flag = 1;
                }
            }
            else
            {
                std::cout << "thread_write..." <<std::endl;
                // IO事件类型：写事件
                // 将响应报文写入，通信套接字的写缓冲区，发送给客户端
                if(request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }

            }
        }

        // default:Proactor，线程池不需要进行数据读取，而是直接开始业务处理
        // 之前主线程的操作已经将数据读取到http的m_read_buf和通信套接字的写缓冲区了
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            // 事件处理模式默认为Proactor
            // 使用同步I/O模拟Proactor
            // 主线程负责epoll实例中的文件描述符监听，以及IO的读写操作(数据读取)
            // 之前的操作已经将数据读取到http的read和write的buffer中了
            // 而工作线程仅仅负责业务处理逻辑(对准备好的数据进行业务逻辑处理)
            request->process();
        }
    }
}