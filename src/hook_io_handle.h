/**
 * @file hook_io_handle.h
 * @author noahyzhang
 * @brief 用于收集 HOOK IO 的信息
 * @version 0.1
 * @date 2023-04-18
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#pragma once

#include <queue>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <algorithm>
#include "common/concurrent_hash_map.h"
#include "common/rw_spin_lock.h"

namespace file_io_hook {

// 默认的数据池最多元素量
#define DEFAULT_MAX_DATA_POOL_SIZE (10000)

/**
 * @brief hook 函数内存监控的项目
 * 
 */
struct HookFuncMonitorItem {
    // open 函数调用次数
    std::atomic<uint64_t> open_func_call_num;
    // close 函数调用次数
    std::atomic<uint64_t> close_func_call_num;
    // read 函数调用次数
    std::atomic<uint64_t> read_func_call_num;
    // write 函数调用次数
    std::atomic<uint64_t> write_func_call_num;
    // open/close 接口参数错误次数
    std::atomic<uint64_t> api_oc_param_error_num;
    // read/write 接口参数错误次数
    std::atomic<uint64_t> api_rw_param_error_num;
    // 超过数据池数量被丢弃的次数
    std::atomic<uint64_t> exceed_data_pool_size_drop_num;
    // 没有发现 fd 和文件名的对应关系的次数
    std::atomic<uint64_t> not_found_fd_file_name_num;
};

/**
 * @brief 文件操作的类型
 * 
 */
enum FileOperateType {
    OPEN_TYPE = 0,
    READ_TYPE,
    WRITE_TYPE,
    CLOSE_TYPE
};

/**
 * @brief 文件的信息
 * 
 */
struct FileInfo {
    uint64_t tid;
    std::string file_name;
    uint64_t read_b;
    uint64_t write_b;
};

/**
 * @brief 双球模型
 * 为了实现高效率的读写，采用双球模型
 * 尽可能保证在同一段时间中写入操作不会出现竞争，这里的一段时间是一个触发条件
 * 同样的，当一个触发条件诞生时，竞争的粒度应该尽可能的小
 * 也就是说，同一段时间只去写一个球 A，触发条件产生，进行切换球，写另外一个球 B，而此时我们再去读球 A
 * 这样就可以保证尽可能的少竞争，性能更佳
 * 
 * 同时，也将数据更进一层抽象化，对外屏蔽掉双球模型的细节。
 * 只提供写和读的接口保证数据的安全
 */
template <typename K, typename V, typename F = std::hash<K>>
struct DoubleBallModule {
public:
    DoubleBallModule() = default;
    ~DoubleBallModule() = default;
    DoubleBallModule(const DoubleBallModule&) = delete;
    DoubleBallModule& operator=(const DoubleBallModule&) = delete;
    DoubleBallModule(DoubleBallModule&&) = delete;
    DoubleBallModule& operator=(DoubleBallModule&&) = delete;

public:
    /**
     * @brief 写数据
     * 当 choose_ball_ 为 true 时，写 ball_01_
     * 当 choose_ball_ 为 false 时，写 ball_01_
     * 
     * @param key 
     * @param value 
     */
    void write(const K& key, const V& value) {
        // rw_spin_lock_.read_lock();
        mtx_.lock();
        ConcurrentHashMap<K, V, F>* ball = choose_ball_ ? &ball_01_ : &ball_02_;
        ball->insert_and_inc(key, value);
        data_count_++;
        // rw_spin_lock_.read_unlock();
        mtx_.unlock();
    }

    /**
     * @brief 读数据
     * 当 choose_ball_ 为 true 时，我们需要读 ball_01_
     * 并且将 choose_ball_ 置为 false，让写数据线程写 ball_02_
     * 实现高效的切换和高效的读写。反之亦然
     * 
     * @return const ConcurrentHashMap<K, V>& 
     */
    ConcurrentHashMap<K, V, F>& read_and_switch() {
        // 此时所有的写线程都在操作球 A，我们对球 B 进行清理。没有竞争，这是线程安全的
        // 因此不用放在锁内
        choose_ball_ ? ball_02_.clear() : ball_01_.clear();
        // 这个读写自旋锁中，写锁中的临界区比较小，效率高
        // rw_spin_lock_.write_lock();
        mtx_.lock();
        ConcurrentHashMap<K, V, F>* res = choose_ball_ ? &ball_01_ : &ball_02_;
        choose_ball_ = !choose_ball_;
        data_count_ = 0;
        // rw_spin_lock_.write_unlock();
        mtx_.unlock();
        // 到这里，已经切换了球，所有的写线程去写另外一个球了，所以操作这个球是线程安全的
        return *res;
    }

    uint64_t size() const {
        return data_count_;
    }

public:
    /**
     * @brief fork 前在父进程上下文执行
     * 
     */
    void lock_prefork() {
        // 这里只需要对 rw_spin_lock_ 加锁即可，无需对两个球加锁
        // 因为能给 rw_spin_lock_ 加到锁，两个球内部不会有线程访问，也不会有锁，锁处于释放状态
        // rw_spin_lock_.write_lock();
        mtx_.lock();
    }

    /**
     * @brief fork 调用创建出子进程后，fork 返回前在父进程上下文中执行
     * 
     */
    void lock_postfork_parent() {
        // rw_spin_lock_.write_unlock();
        mtx_.unlock();
    }

    /**
     * @brief fork 调用创建子进程后，fork 返回前在子进程上下文中执行
     * 
     */
    void lock_postfork_child() {
        // rw_spin_lock_.write_unlock();
        mtx_.unlock();
    }

private:
    volatile bool choose_ball_ = true;
    ConcurrentHashMap<K, V, F> ball_01_;
    ConcurrentHashMap<K, V, F> ball_02_;
    // 读写自旋锁
    // RWSpinLock rw_spin_lock_;
    std::mutex mtx_;
    // 当前数据池中数据的数量
    volatile uint64_t data_count_;
};

/**
 * @brief 检测 FileIoInfoHandler 对象是否被销毁
 * 为了检查进程退出时，先销毁了业务对象，然后调用 exit，进而调用 close/fclose 导致使用了被 hook 的 close/fclose 函数
 * 而 hook 的 close/fclose 会使用相关业务对象
 * 暂时通过一个全局变量来规避，当业务对象销毁时，让全局变量置为 true，并且不再调用 hook 函数
 */
static bool is_object_destruct = false;

/**
 * @brief 收集文件的 IO 信息
 * 特别注意：当进程退出时，会调用 exit，而 exit 会通过 close/fclose 关闭打开的文件描述符
 * 而此之前，业务对象可能已经被销毁，此时如果访问的话会导致 core（非法的内存访问）
 */
class FileIoInfoHandler {
public:
    ~FileIoInfoHandler() {
        is_object_destruct = true;
    }
    FileIoInfoHandler(const FileIoInfoHandler&) = delete;
    FileIoInfoHandler& operator=(const FileIoInfoHandler&) = delete;
    FileIoInfoHandler(FileIoInfoHandler&&) = delete;
    FileIoInfoHandler& operator=(FileIoInfoHandler&&) = delete;
    /**
     * @brief 单例模式
     * 
     * @return FileIoInfoHandler& 
     */
    static FileIoInfoHandler& get_instance() {
        static FileIoInfoHandler instance;
        return instance;
    }

public:
    /**
     * @brief 添加 open/close hook io 函数的信息
     *  主要是填充 fd 和文件名的对应关系
     * 
     * @param type 
     * @param fd 
     * @param file_name 
     */
    void add_hook_info(FileOperateType type, int fd, const char* file_name);

    /**
     * @brief 添加 read/write hook io 函数的信息
     *  注意：此函数内不可添加 IO 类函数，否则可能会造成死循环
     *  比如：hook_write 调用 add_hook_info，而 add_hook_info 使用 IO 函数的话又相当于调用了 hook_write
     * @param type 
     * @param rw_size 
     */
    void add_hook_info(FileOperateType type, int fd, size_t rw_size);

    /**
     * @brief 消费所有信息，并且解析后返回
     * 
     * @return const std::vector<FileInfo>& 
     */
    const std::vector<FileInfo>& consume_and_parse();

    /**
     * @brief Set the destruct status object
     * 
     */
    void set_destruct_status() {
        is_object_destruct = true;
    }

public:
    /**
     * @brief fork 前在父进程上下文执行
     * 
     */
    void lock_prefork() {
        // 这两个没有顺序区分
        data_pool_.lock_prefork();
        fd_file_name_.lock_prefork();
    }

    /**
     * @brief fork 返回前，在父进程上下文执行
     * 
     */
    void lock_postfork_parent() {
        data_pool_.lock_postfork_parent();
        fd_file_name_.lock_postfork_parent();
    }

    /**
     * @brief fork 返回前，在子进程上下文执行
     * 
     */
    void lock_postfork_child() {
        data_pool_.lock_postfork_child();
        fd_file_name_.lock_postfork_child();
    }

private:
    // 分割符
    static const char SEPARATOR_CHAR = '-';

    /**
     * @brief 合并&生成 key
     * 
     * @param tid 
     * @param file_name 
     * @return std::string 
     */
    inline std::string combine_key(uint64_t tid, const std::string& file_name);

    /**
     * @brief 分解 key
     * 
     * @param key 
     * @param tid 
     * @param file_name 
     * @return int 
     */
    int divide_key(const std::string& key, uint64_t* tid, std::string* file_name);

private:
    FileIoInfoHandler() = default;

private:
    struct FileRWInfo {
        uint64_t read_b;
        uint64_t write_b;

        FileRWInfo& operator+=(const FileRWInfo& info) {
            read_b += info.read_b;
            write_b += info.write_b;
            return *this;
        }
    };
    struct DoubleBallModuleKey {
        uint64_t tid;
        std::string filename;
        DoubleBallModuleKey(uint64_t tid, const std::string& filename)
            : tid(tid), filename(filename) {}

        bool operator==(const DoubleBallModuleKey& key) const {
            return tid == key.tid && filename == key.filename;
        }
        bool operator!=(const DoubleBallModuleKey& key) const {
            return !(*this == key);
        }
    };
    struct DoubleBallModuleKeyHash {
        std::size_t operator()(const DoubleBallModuleKey& obj) const {
            std::size_t h1 = std::hash<uint64_t>()(obj.tid);
            std::size_t h2 = std::hash<std::string>()(obj.filename);
            return h1 ^ (h2 << 1);
        }
    };

private:
    // 数据池子，只管写数据、读数据，无需关心线程安全性，已经保证
    // key 为 "tid + file_name"
    // DoubleBallModule<std::shared_ptr<DoubleBallModuleKey>, FileRWInfo> data_pool_;
    DoubleBallModule<DoubleBallModuleKey, FileRWInfo, DoubleBallModuleKeyHash> data_pool_;
    // 默认的数据池中最大的元素数量
    const uint64_t max_data_pool_size_ = DEFAULT_MAX_DATA_POOL_SIZE;
    // 存储文件描述符和文件名的对应关系
    ConcurrentHashMap<uint64_t, std::string> fd_file_name_;
    // hook 函数监控项目
    HookFuncMonitorItem monitor_item;
};

}  // namespace file_io_hook
