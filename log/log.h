#pragma once
#include <string>
#include "block_queue.h"


/********************日志类***********************/
class Log
{
public:
    // 静态接口，获取日志类实例
    // C++11以后,使用局部变量懒汉不用加锁
    static Log *get_instance();
    // 异步写日志公有方法，调用私有方法async_write_log
    static void *flush_log_thread(void *args);

    //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192,
              int split_lines = 5000000, int max_queue_size = 0);
    // 将输出内容按照标准格式整理
    void write_log(int level, const char *format, ...);
    // 强制刷新缓冲区
    void flush(void);

private:
    // 私有化构造函数
    Log();
    virtual ~Log();
    // 异步写日志方法
    void *async_write_log();

private:
    // 路径名
    char dir_name[128];
    // log文件名
    char log_name[128];
    // 日志最大行数
    int m_split_lines;
    // 日志缓冲区大小
    int m_log_buf_size;
    // 日志行数记录
    long long m_count;
    // 因为按天分类,记录当前时间是那一天
    int m_today;
    // 打开log的文件指针
    FILE *m_fp;
    // 要输出的内容
    char *m_buf;
    // 阻塞队列
    block_queue<std::string> *m_log_queue;
    // 是否同步标志位
    bool m_is_async;
    // 同步类
    locker m_mutex;
    // 关闭日志
    int m_close_log;
};
/********************日志类***********************/


/******************使用宏进行日志输出****************/
// 日志等级进行分类，包括DEBUG，INFO，WARN和ERROR四种级别的日志
// 先判断是否将日志关闭，后，调用类的函数write_log向日志文件写入日志信息，最后对写缓冲区进行刷新
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}
/******************使用宏进行日志输出****************/