/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/
#pragma once
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"


// 模板类
template <class T>
class block_queue
{
public:
    // 构造函数初始化私有成员
    block_queue(int max_size = 1000);
    ~block_queue();
    void clear();
    bool full();
    bool empty();
    bool front(T &value);
    bool back(T &value);
    int size();
    int max_size();
    bool push(const T &item);
    bool pop(T &item);
    bool pop(T &item, int ms_timeout);

private:
    // 互斥锁，locker类实例 m_mutex，该锁在locker的构造函数中进行了初始化
    locker m_mutex;
    // 造函数中进行了初始化
    cond m_cond;
    T *m_array;
    // 队列中当前元素数量
    int m_size;
    // 队列容量
    int m_max_size;
    // 队列首部索引
    int m_front;
    // 队列尾部索引
    int m_back;
};


/*
 * func:构造函数初始化私有成员
 */
template <class T>
block_queue<T>::block_queue(int max_size)
{
    if (max_size <= 0)
    {
        exit(-1);
    }
    m_max_size = max_size;
    // 循环数组实现阻塞队列
    m_array = new T[max_size];
    m_size = 0;
    m_front = -1;
    m_back = -1;
}


/*
 * func:析构函数，资源回收
 */
template <class T>
block_queue<T>::~block_queue()
{
    m_mutex.lock();
    if (m_array != NULL)
        delete [] m_array;

    m_mutex.unlock();
}


/*
 * func:清空私有的成员变量
 */
template<class T>
void block_queue<T>::clear()
{
    m_mutex.lock();
    m_size = 0;
    m_front = -1;
    m_back = -1;
    m_mutex.unlock();
}


/*
 * func:判断队列是否满
 */
template<class T>
bool block_queue<T>::full()
{
    m_mutex.lock();
    if (m_size >= m_max_size)
    {

        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}


/*
 * func:判断队列是否为空
 */
template<class T>
bool block_queue<T>::empty()
{
    m_mutex.lock();
    if (0 == m_size)
    {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}


/*
 * func:返回队首元素
 */
template<class T>
bool block_queue<T>::front(T &value)
{
    m_mutex.lock();
    if (0 == m_size)
    {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_front];
    m_mutex.unlock();
    return true;
}


/*
 * func:返回队尾元素
 */
template<class T>
bool block_queue<T>::back(T &value)
{
    m_mutex.lock();
    if (0 == m_size)
    {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_back];
    m_mutex.unlock();
    return true;
}


/*
 * func:获取队列元素数量
 */
template<class T>
int block_queue<T>::size()
{
    int tmp = 0;

    m_mutex.lock();
    tmp = m_size;

    m_mutex.unlock();
    return tmp;
}


/*
 * func:获取队列容量
 */
template<class T>
int block_queue<T>::max_size()
{
    int tmp = 0;

    m_mutex.lock();
    tmp = m_max_size;

    m_mutex.unlock();
    return tmp;
}


/*
 * func:向队列中添加元素
 * note:往队列添加元素后，需要将所有使用队列的消费者线程唤醒
 */
template<class T>
bool block_queue<T>::push(const T &item)
{
    m_mutex.lock();
    // 队列是满的，无法继续添加
    if (m_size >= m_max_size)
    {
        // 唤醒阻塞在队列上的消费者
        m_cond.broadcast();
        m_mutex.unlock();
        return false;
    }
    // 队尾索引向后移动
    m_back = (m_back + 1) % m_max_size;
    // 入队
    m_array[m_back] = item;
    m_size++;

    // 唤醒阻塞在队列上的消费者
    m_cond.broadcast();
    m_mutex.unlock();
    return true;
}


/*
 * func:从队列中取出元素
 * note:pop时,如果当前队列为可空,将会等待条件变量，阻塞所有等待在队列上的消费这线程
 */
template<class T>
bool block_queue<T>::pop(T &item)
{
    m_mutex.lock();
    // 队列为空
    while (m_size <= 0)
    {
        // 消费者阻塞在队列上，等待生产者唤醒
        if (!m_cond.wait(m_mutex.get()))
        {
            m_mutex.unlock();
            return false;
        }
    }

    // 队首索引，向后移动
    m_front = (m_front + 1) % m_max_size;
    // 出队
    item = m_array[m_front];
    m_size--;
    m_mutex.unlock();
    return true;
}


/*
 * func:从队列中取出元素
 * note:增加了超时处理，若超时时间内没有抢到，则return false
 */
template<class T>
bool block_queue<T>::pop(T &item, int ms_timeout)
{
    struct timespec t = {0, 0};
    struct timeval now = {0, 0};
    // 获取当前的时间  秒和微秒
    gettimeofday(&now, NULL);
    m_mutex.lock();
    if (m_size <= 0)
    {
        // 队列为空，计算超时时间
        // ms_timeout/1000毫秒超时转换为秒
        t.tv_sec = now.tv_sec + ms_timeout / 1000;
        t.tv_nsec = (ms_timeout % 1000) * 1000;
        if (!m_cond.timeWait(m_mutex.get(), t))
        {
            m_mutex.unlock();
            return false;
        }
    }

    if (m_size <= 0)
    {
        m_mutex.unlock();
        return false;
    }

    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size--;
    m_mutex.unlock();
    return true;
}