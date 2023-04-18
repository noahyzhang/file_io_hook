/**
 * @file common.h
 * @author noahyzhang
 * @brief 
 * @version 0.1
 * @date 2023-04-18
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#pragma once

#include <sys/syscall.h>
#include <unistd.h>
#include <stdint.h>

namespace file_io_hook {

/**
 * @brief hook 相关的工具类
 * 
 */
class Util {
public:
    /**
     * @brief 获取线程的 tid，使用 TLS 进行优化
     * 
     * @return int64_t 
     */
    static int64_t get_tid() {
        static __thread int64_t tid = -1;
        if (tid == -1) {
            tid = syscall(SYS_gettid);
        }
        return tid;
    }
};

}  // namespace file_io_hook
