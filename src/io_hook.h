/**
 * @file io_hook.h
 * @author noahyzhang
 * @brief IO 系统调用的 hook
 * @version 0.1
 * @date 2023-04-18
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#pragma once

#include <sys/types.h>
#include <stdio.h>

/*
 * 1. HOOK io 操作的系统调用尽量在其内部不要使用 io 操作，否则可能出现死循环。待一个更好的解决方案
 * 2. 为了精简化，目前只计算文件的读写情况，对于打开、关闭的操作暂时不关注，以免太多杂糅，影响本身功能
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 本文件用于 hook 操作 IO 的系统调用
 */

/*
 * 文件的打开和创建相关系统调用
 * fd 参数把 open 和 openat 函数区分开
 * 1. path 参数指定的是绝对路径名，这种情况下，fd 参数被忽略，openat 函数等效于 open 函数
 * 2. path 参数指定的是相对路径名，fd 参数指出了相对路径名在文件系统中的开始地址。
 *    fd 参数是通过打开相对路径名所在的目录来获取
 * 3. path 参数指定了相对路径名，fd 参数具有特殊值 AT_FDCWD。
 *    这种情况下，路径名在当前工作目录中获取，openat 函数在操作上与 open 函数类似
 * 4. 后缀为 64 的意为大文件
*/
extern int open(const char *pathname, int flags, ...) __nonnull((1));
extern int open64(const char *file, int flag, ...) __nonnull((1));
extern int creat(const char *pathname, mode_t mode) __nonnull((1));
extern int creat64(const char *file, mode_t mode) __nonnull((1));
extern int openat(int dirfd, const char *pathname, int flags, ...) __nonnull((2));
extern int openat64(int dirfd, const char *file, int oflag, ...) __nonnull((2));

/*
 * 文件的读写相关的系统调用
 * pread 和 pwrite 相当于是调用 lseek 后调用 read/write
 * 主要是为了解决早期的 open 不支持 O_APPEND 选项，追加写操作需要 lseek 来设置偏移量
 * 导致一次完整的写操作需要调用两个系统调用，不是原子操作。多进程操作相同文件会出现问题
 * 后缀为 64 的意为大文件
 */
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern ssize_t pread(int fd, void *buf, size_t count, off_t offset);
extern ssize_t pread64(int fd, void *buf, size_t nbytes, __off64_t offset);
extern ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
extern ssize_t pwrite64(int fd, const void *buf, size_t n, __off64_t offset);

/*
 * 当一个进程终止时，内核会自动关闭它所有打开的文件
 * 读写完文件不关闭可能会造成文件描述符泄漏
 */
extern int close(int fd);

/*
 * 如下为带缓冲的 IO，比如：fopen、fread、fwrite、fclose 之类
 * 这些系统调用的实现不一定是 open/read/write/close 之类的，在 GUN C 库中，他的实现可能为 mmap
 * 而且带缓冲的 IO 的实现不一定是直接使用 open/read/write/close 来与文件/磁盘交互
 * 因此这些带缓冲的 IO 也需要 hook
 * 
 * 对于标准输入、标准输出、标准错误暂不进行监控，因此项目一般不会用到，并且这类问题易于处理
 * 
 * 对于字符操作，比如：fgetc/getc/fputc/putc 等等暂不监控
 * 对于格式化字符串，然后输入输出的调用，比如：fprintf 等暂不监控
 * 总之，我们只 hook 常用的流相关系统调用
 * 如下，主要来源于 stdio.h 头文件
 */

// 打开文件并创建一个新的流
extern FILE *fopen(const char *__restrict filename, const char *__restrict modes);

// 打开大文件
extern FILE *fopen64(const char *__restrict filename, const char *__restrict modes);

// 打开文件名为 pathname 的文件，并关联 stream 所指向的流。原始流被关闭
extern FILE *freopen(const char *pathname, const char *mode, FILE *stream);

// 从流中读取数据
extern size_t fread(void *__restrict ptr, size_t size, size_t n, FILE *__restrict stream);

// 向流中写入数据
extern size_t fwrite(const void *__restrict ptr, size_t size, size_t n, FILE *__restrict stream);

// 关闭流
extern int fclose(FILE *stream);

#ifdef __cplusplus
}
#endif
