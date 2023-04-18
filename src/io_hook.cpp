#include <dlfcn.h>
#include <stddef.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <stdint.h>
#include "hook_io_handle.h"
#include "io_hook.h"

/* 
 * 定义需要 hook 的函数类型
 */
// 不带缓冲的操作 IO 的函数类型
typedef int (*open_func_type)(const char *pathname, int flags, ...);
typedef int (*open64_func_type)(const char *__file, int __oflag, ...);
typedef int (*creat_func_type)(const char *pathname, mode_t mode);
typedef int (*creat64_func_type)(const char *__file, mode_t __mode);
typedef int (*openat_func_type)(int dirfd, const char *pathname, int flags, ...);
typedef int (*openat64_func_type)(int __fd, const char *__file, int __oflag, ...);
typedef ssize_t (*read_func_type)(int fd, void *buf, size_t count);
typedef ssize_t (*write_func_type)(int fd, const void *buf, size_t count);
typedef ssize_t (*pread_func_type)(int fd, void *buf, size_t count, off_t offset);
typedef ssize_t (*pread64_func_type)(int __fd, void *__buf, size_t __nbytes, __off64_t __offset);
typedef ssize_t (*pwrite_func_type)(int fd, const void *buf, size_t count, off_t offset);
typedef ssize_t (*pwrite64_func_type)(int __fd, const void *__buf, size_t n, __off64_t __offset);
typedef int (*close_func_type)(int fd);

// 带缓冲的操作 IO 的函数类型
typedef FILE* (*fopen_func_type)(const char *__restrict filename, const char *__restrict modes);
typedef FILE* (*fopen64_func_type)(const char *__restrict filename, const char *__restrict modes);
typedef FILE* (*freopen_func_type)(const char *pathname, const char *mode, FILE *stream);
typedef size_t (*fread_func_type)(void *__restrict ptr, size_t size, size_t n, FILE *__restrict stream);
typedef size_t (*fwrite_func_type)(const void *__restrict ptr, size_t size, size_t n, FILE *__restrict __s);
typedef int (*fclose_func_type)(FILE *stream);

// 加载文件 IO 信息收集类
using file_io_hook::FileIoInfoHandler;
using file_io_hook::FileOperateType;

// 定义数组 file_io_real_func_point 的长度
// 注意增加宏定义，需要增加长度
#define FILE_IO_FUNC_TYPE_COUNT 19
// 定义文件 IO 函数宏定义，作为数组的下标.
typedef enum FILE_IO_FUNC_TYPE {
    OPEN_FUNC_TYPE = 0,
    OPEN64_FUNC_TYPE,
    CREAT_FUNC_TYPE,
    CREAT64_FUNC_TYPE,
    OPENAT_FUNC_TYPE,
    OPENAT64_FUNC_TYPE,
    READ_FUNC_TYPE,
    WRITE_FUNC_TYPE,
    PREAD_FUNC_TYPE,
    PREAD64_FUNC_TYPE,
    PWRITE_FUNC_TYPE,
    PWRITE64_FUNC_TYPE,
    CLOSE_FUNC_TYPE,
    FOPEN_FUNC_TYPE,
    FOPEN64_FUNC_TYPE,
    FREOPEN_FUNC_TYPE,
    FREAD_FUNC_TYPE,
    FWRITE_FUNC_TYPE,
    FCLOSE_FUNC_TYPE,
} FILE_IO_FUNC_TYPE;

// 存储 IO 函数指针
// 注意：不能使用 STL 容器，STL 容器的初始化在 constructor 中会有问题
static void* file_io_real_func_pointer[FILE_IO_FUNC_TYPE_COUNT] = {0};

// 封装存储 IO 函数指针的数组，避免用户直接使用数组
static void* get_real_func_pointer(FILE_IO_FUNC_TYPE func_type) {
    return file_io_real_func_pointer[func_type];
}

// 初始化函数
static void io_hook_init() {
    // 注意：要保证数组长度，而且要保证和 FILE_IO_FUNC_TYPE 定义的顺序一致
    static const char* hook_func[FILE_IO_FUNC_TYPE_COUNT] = {
        "open", "open64", "creat", "creat64", "openat", "openat64",
        "read", "write", "pread", "pread64", "pwrite", "pwrite64", "close",
        "fopen", "fopen64", "freopen", "fread", "fwrite", "fclose"};
    for (size_t i = 0; i < sizeof(hook_func)/sizeof(const char*); ++i) {
        file_io_real_func_pointer[i] = dlsym(RTLD_NEXT, hook_func[i]);
    }
}

// fork 调用前，在父进程的上下文中执行
static void io_hook_prefork() {
    FileIoInfoHandler::get_instance().lock_prefork();
}

// fork 返回前，在父进程的上下文中执行
static void io_hook_postfork_parent() {
    FileIoInfoHandler::get_instance().lock_postfork_parent();
}

// fork 返回前，在子进程的上下文执行
static void io_hook_postfork_child() {
    FileIoInfoHandler::get_instance().lock_postfork_child();
}

// 处理多进程的共享资源（锁）问题
static void init_hard_atfork() {
    int res = pthread_atfork(io_hook_prefork, io_hook_postfork_parent, io_hook_postfork_child);
    if (res != 0) {
        // 系统调用如果错误，这种情况很糟糕，没有较好的处理办法
        // 如果允许 abort，直接 abort，本功能非必须，不要 abort
        // 直接关掉文件 IO 功能
        FileIoInfoHandler::get_instance().set_destruct_status();
    }
}

// 系统自动调用
__attribute__((constructor)) static void io_hook_constructor() {
    io_hook_init();
    init_hard_atfork();
}

// ----------- 重写 IO hook 函数 ---------------

int open(const char *pathname, int flags, ...) {
    static open_func_type real_open = (open_func_type)get_real_func_pointer(OPEN_FUNC_TYPE);
    if (__glibc_unlikely(!real_open)) {
        return -1;
    }
    va_list args;
    va_start(args, flags);
    int ret = real_open(pathname, flags, args);
    va_end(args);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::OPEN_TYPE, ret, pathname);
    }
    return ret;
}

int open64(const char *file, int flag, ...) {
    static open64_func_type real_open64 = (open64_func_type)get_real_func_pointer(OPEN64_FUNC_TYPE);
    if (__glibc_unlikely(!real_open64)) {
        return -1;
    }
    va_list args;
    va_start(args, flag);
    int ret = real_open64(file, flag, args);
    va_end(args);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::OPEN_TYPE, ret, file);
    }
    return ret;
}

int creat(const char *pathname, mode_t mode) {
    static creat_func_type real_creat = (creat_func_type)get_real_func_pointer(CREAT_FUNC_TYPE);
    if (__glibc_unlikely(!real_creat)) {
        return -1;
    }
    int ret = real_creat(pathname, mode);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::OPEN_TYPE, ret, pathname);
    }
    return ret;
}

int creat64(const char *file, mode_t mode) {
    static creat64_func_type real_creat64 = (creat64_func_type)get_real_func_pointer(CREAT64_FUNC_TYPE);
    if (__glibc_unlikely(!real_creat64)) {
        return -1;
    }
    int ret = real_creat64(file, mode);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::OPEN_TYPE, ret, file);
    }
    return ret;
}

int openat(int dirfd, const char *pathname, int flags, ...) {
    static openat_func_type real_openat = (openat_func_type)get_real_func_pointer(OPENAT_FUNC_TYPE);
    if (__glibc_unlikely(!real_openat)) {
        return -1;
    }
    va_list args;
    va_start(args, flags);
    int ret = real_openat(dirfd, pathname, flags, args);
    va_end(args);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::OPEN_TYPE, ret, pathname);
    }
    return ret;
}

int openat64(int dirfd, const char *file, int flag, ...) {
    static openat64_func_type real_openat64 = (openat64_func_type)get_real_func_pointer(OPENAT64_FUNC_TYPE);
    if (__glibc_unlikely(!real_openat64)) {
        return -1;
    }
    va_list args;
    va_start(args, flag);
    int ret = real_openat64(dirfd, file, flag, args);
    va_end(args);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::OPEN_TYPE, ret, file);
    }
    return ret;
}

ssize_t read(int fd, void *buf, size_t count) {
    static read_func_type real_read = (read_func_type)get_real_func_pointer(READ_FUNC_TYPE);
    if (__glibc_unlikely(!real_read)) {
        return -1;
    }
    ssize_t ret = real_read(fd, buf, count);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::READ_TYPE, fd, ret);
    }
    return ret;
}

ssize_t write(int fd, const void *buf, size_t count) {
    static write_func_type real_write = (write_func_type)get_real_func_pointer(WRITE_FUNC_TYPE);
    if (__glibc_unlikely(!real_write)) {
        return -1;
    }
    ssize_t ret = real_write(fd, buf, count);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::WRITE_TYPE, fd, ret);
    }
    return ret;
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
    static pread_func_type real_pread = (pread_func_type)get_real_func_pointer(PREAD_FUNC_TYPE);
    if (__glibc_unlikely(!real_pread)) {
        return -1;
    }
    ssize_t ret = real_pread(fd, buf, count, offset);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::READ_TYPE, fd, ret);
    }
    return ret;
}

ssize_t pread64(int fd, void *buf, size_t nbytes, __off64_t offset) {
    static pread64_func_type real_pread64 = (pread64_func_type)get_real_func_pointer(PREAD64_FUNC_TYPE);
    if (__glibc_unlikely(!real_pread64)) {
        return -1;
    }
    ssize_t ret = real_pread64(fd, buf, nbytes, offset);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::READ_TYPE, fd, ret);
    }
    return ret;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
    static pwrite_func_type real_pwrite = (pwrite_func_type)get_real_func_pointer(PWRITE_FUNC_TYPE);
    if (__glibc_unlikely(!real_pwrite)) {
        return -1;
    }
    ssize_t ret = real_pwrite(fd, buf, count, offset);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::WRITE_TYPE, fd, ret);
    }
    return ret;
}

ssize_t pwrite64(int fd, const void *buf, size_t n, __off64_t offset) {
    static pwrite64_func_type real_pwrite64 = (pwrite64_func_type)get_real_func_pointer(PWRITE64_FUNC_TYPE);
    if (__glibc_unlikely(!real_pwrite64)) {
        return -1;
    }
    ssize_t ret = real_pwrite64(fd, buf, n, offset);
    if (ret >= 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::WRITE_TYPE, fd, ret);
    }
    return ret;
}

int close(int fd) {
    static close_func_type real_close = (close_func_type)get_real_func_pointer(CLOSE_FUNC_TYPE);
    if (__glibc_unlikely(!real_close)) {
        return -1;
    }
    int ret = real_close(fd);
    if (ret == 0) {
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::CLOSE_TYPE, fd, "");
    }
    return ret;
}

FILE *fopen(const char *__restrict filename, const char *__restrict modes) {
    static fopen_func_type real_fopen = (fopen_func_type)get_real_func_pointer(FOPEN_FUNC_TYPE);
    if (__glibc_unlikely(!real_fopen)) {
        return NULL;
    }
    FILE* stream = real_fopen(filename, modes);
    if (stream != NULL) {
        int fd = fileno(stream);
        if (fd < 0) return stream;
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::OPEN_TYPE, fd, filename);
    }
    return stream;
}

FILE *fopen64(const char *__restrict filename, const char *__restrict modes) {
    static fopen64_func_type real_fopen64 = (fopen64_func_type)get_real_func_pointer(FOPEN64_FUNC_TYPE);
    if (__glibc_unlikely(!real_fopen64)) {
        return NULL;
    }
    FILE* stream = real_fopen64(filename, modes);
    if (stream != NULL) {
        int fd = fileno(stream);
        if (fd < 0) return stream;
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::OPEN_TYPE, fd, filename);
    }
    return stream;
}

FILE *freopen(const char *pathname, const char *mode, FILE *stream) {
    static freopen_func_type real_freopen = (freopen_func_type)get_real_func_pointer(FREOPEN_FUNC_TYPE);
    if (__glibc_unlikely(!real_freopen)) {
        return NULL;
    }
    FILE* new_stream = real_freopen(pathname, mode, stream);
    if (new_stream != NULL) {
        int fd = fileno(new_stream);
        if (fd < 0) return new_stream;
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::OPEN_TYPE, fd, pathname);
    }
    return new_stream;
}

size_t fread(void *__restrict ptr, size_t size, size_t n, FILE *__restrict stream) {
    static fread_func_type real_fread = (fread_func_type)get_real_func_pointer(FREAD_FUNC_TYPE);
    if (__glibc_unlikely(!real_fread)) {
        return 0;
    }
    size_t ret = real_fread(ptr, size, n, stream);
    int fd = fileno(stream);
    if (fd < 0) return ret;
    FileIoInfoHandler::get_instance().add_hook_info(
        FileOperateType::READ_TYPE, fd, (ret*size));
    return ret;
}

size_t fwrite(const void *__restrict ptr, size_t size, size_t n, FILE *__restrict stream) {
    static fwrite_func_type real_fwrite = (fwrite_func_type)get_real_func_pointer(FWRITE_FUNC_TYPE);
    if (__glibc_unlikely(!real_fwrite)) {
        return 0;
    }
    ssize_t ret = real_fwrite(ptr, size, n, stream);
    int fd = fileno(stream);
    if (fd < 0) return ret;
    FileIoInfoHandler::get_instance().add_hook_info(
        FileOperateType::WRITE_TYPE, fd, (ret*size));
    return ret;
}

int fclose(FILE *stream) {
    static fclose_func_type real_fclose = (fclose_func_type)get_real_func_pointer(FCLOSE_FUNC_TYPE);
    if (__glibc_unlikely(!real_fclose)) {
        return -1;
    }
    // 在流关闭前获取文件描述符
    int fd = fileno(stream);
    int ret = real_fclose(stream);
    if (ret == 0) {
        if (fd < 0) return ret;
        FileIoInfoHandler::get_instance().add_hook_info(
            FileOperateType::CLOSE_TYPE, fd, "");
    }
    return ret;
}
