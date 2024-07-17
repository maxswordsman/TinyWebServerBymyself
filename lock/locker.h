#pragma once
#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 互斥锁类
class locker{
public:
    locker()
    {
        if(pthread_mutex_init(&m_mutex,NULL)!=0)
        {
            throw std::exception();  // 互斥锁初始化成功return 0,否则非0，抛出异常
        }
    }

    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0; // return 0表示加锁成功
    }

    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    // 定义一个私有的互斥锁变量
    pthread_mutex_t m_mutex;
};


// 条件变量类
class cond{
public:
    cond()
    {
        if(pthread_cond_init(&m_cond,NULL) != 0)
        {
            throw std::exception();  // 条件变量初始化成功return 0
        }
    }

    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        ret = pthread_cond_wait(&m_cond,m_mutex); // 线程阻塞函数
        return ret==0;
    }

    // 设置阻塞时间
    bool timeWait(pthread_mutex_t *m_mutex,struct timespec t)
    {
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond,m_mutex,&t); // 设置了阻塞时间的线程阻塞函数
        return ret==0;
    }

    // 唤醒单个阻塞线程
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0; // 唤醒阻塞在线程阻塞函数上的单个线程
    }

    // 广播唤醒所有阻塞线程
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0; // 唤醒阻塞在线程阻塞函数上的所有线程
    }
private:
    pthread_cond_t m_cond;
};


// 信号量类
class sem{
public:
    sem()
    {
        // 参数3，设置的信号量为0
        if(sem_init(&m_sem,0,0) != 0)
        {
            throw std::exception();
        }
    }

    sem(int num){
        if(sem_init(&m_sem,0,num) != 0)
        {
            throw std::exception();
        }
    }

    ~sem()
    {
        sem_destroy(&m_sem);
    }

    // 等待信号量
    bool wait()
    {
        // 信号量，m_sem不为0，则sem_wait return 0
        // 信号量，m_sem为0，则sem_wait 阻塞线程
        return sem_wait(&m_sem) == 0;
    }

    // 增加信号量
    bool post()
    {
        // 增加信号量，并且唤醒一个阻塞在sem_wait() 上的线程
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t  m_sem;
};