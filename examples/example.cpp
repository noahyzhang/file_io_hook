#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "hook_io_handle.h"

void test_hook_io_function(const char* file_name) {
    umask(0);
    int fd = open(file_name, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "call open failed, err: %s\n", strerror(errno));
        return;
    }
    char buf[] = "hello world!\n";
    ssize_t res = write(fd, buf, sizeof(buf));
    if (res < 0) {
        fprintf(stderr, "call write failed, err: %s\n", strerror(errno));
        close(fd);
        return;
    } else {
        fprintf(stdout, "write file: %s success, write bytes: %lu\n", file_name, res);
    }
    fsync(fd);
    close(fd);

    int read_fd = open(file_name, O_RDONLY);
    if (read_fd < 0) {
        fprintf(stderr, "call open for read failed, err: %s\n", strerror(errno));
        return;
    }
    char receive_buf[1024] = {0};
    res = read(read_fd, receive_buf, 10);
    if (res < 0) {
        fprintf(stderr, "call read failed, err: %s\n", strerror(errno));
        close(read_fd);
        return;
    } else {
        fprintf(stdout, "read file: %s success, read bytes: %lu\n", file_name, res);
    }
    close(read_fd);
    remove(file_name);
}

int main() {
    // 构造文件读写的场景
    const int ARR_SIZE = 3;
    const char* test_file_arr[ARR_SIZE] = {"test_01.txt", "test_02.txt", "test_03.txt"};
    for (int i = 0; i < ARR_SIZE; ++i) {
        test_hook_io_function(test_file_arr[i]);
    }

    // 获取文件读写信息
    auto& file_infos = file_io_hook::FileIoInfoHandler::get_instance().consume_and_parse();
    for (const auto& info : file_infos) {
        fprintf(stdout, "file r/w info: tid: %lu, name: %s, read(B): %lu, write(B): %lu\n",
            info.tid, info.file_name.c_str(), info.read_b, info.write_b);
    }
    return 0;
}
