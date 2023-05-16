#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdint.h>
#include <vector>
#include "hook_io_handle.h"

uint64_t cost_total_time_us = 0;
int cycle_count = 0;
int fd_count = 1000;
int fd_arr[1000];

void open_multi_file() {
    for (int i = 0; i < fd_count; i++) {
        std::string file_name = "/tmp/test_" + std::to_string(i) + ".txt";
        int fd = open(file_name.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0666);
        if (fd == -1) {
            perror("open");
            return;
        }
        fd_arr[i] = fd;
    }
}

void performIOOperations() {
    int rand_num = rand() % fd_count;
    int fd = fd_arr[rand_num];

    ssize_t bytesRead, bytesWritten;
    if (fd % 2 == 0) {
        char buffer[1024];
        bytesRead = read(fd, buffer, sizeof(buffer));
        if (bytesRead == -1) {
            perror("read");
            close(fd);
            return;
        }
    } else {
        char buffer[1024] = "hello world";
        bytesWritten = write(fd, buffer, sizeof(buffer));
        if (bytesWritten == -1) {
            perror("write");
            close(fd);
            return;
        }
    }

    // close(fd);
}

void multiThreadIOOperations(std::vector<pthread_t>& threads) {
    for (size_t i = 0; i < threads.size(); i++) {
        pthread_create(&threads[i], nullptr, [](void*)->void* {
            struct timeval start, end;
            int64_t totalMicroseconds;
            gettimeofday(&start, NULL);
            // Perform a large number of IO operations
            for (int i = 0; i < cycle_count; i++) {
                performIOOperations();
            }
            gettimeofday(&end, NULL);
            // Calculate total time in microseconds
            totalMicroseconds = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
            cost_total_time_us += totalMicroseconds;
            return nullptr;
        }, nullptr);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <filename> <thread_count> <cycle_count>\n", argv[0]);
        return -1;
    }
    int thread_count = atoi(argv[1]);
    cycle_count = atoi(argv[2]);

    open_multi_file();
    srand(time(NULL));

    std::vector<pthread_t> threads(thread_count, 0);
    multiThreadIOOperations(threads);

    pthread_t gather_data_thread;
    pthread_create(&gather_data_thread, nullptr, [](void*)->void* {
        for (;;) {
            const auto& file_io_infos = file_io_hook::FileIoInfoHandler::get_instance().consume_and_parse();
            // if (file_io_infos.size() != 0) {
                printf("receive io info size: %lu\n", file_io_infos.size());
            // }
            sleep(1);
        }
        return nullptr;
    }, nullptr);

    for (size_t i = 0; i < threads.size(); i++) {
        pthread_join(threads[i], nullptr);
    }

    printf("Total time per thread: %lu microseconds\n", cost_total_time_us);
    pthread_join(gather_data_thread, nullptr);
    return 0;
}

// 机器：16核心
// 测试结果

// benchmark_normal
// # ./benchmark_normal 10 10000
// Total time per thread: 3674515 microseconds

// # ./benchmark_normal 10 100000
// Total time per thread: 37157857 microseconds

// # ./benchmark_normal 10 1000000
// Total time per thread: 373828445 microseconds
// CPU: 858%

// benchmark_hook
// # ./benchmark_hook 10 10000
// Total time per thread: 3655167 microseconds

// # ./benchmark_hook 10 100000
// Total time per thread: 37028799 microseconds

// # ./benchmark_hook 10 1000000
// Total time per thread: 373871056 microseconds
// CPU: 864.1%
