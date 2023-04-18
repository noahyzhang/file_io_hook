## 文件 IO 监控

我们的项目在运行过程中，我们有极强的需求将程序对 IO 的使用监控起来。以便可以及时发现读写异常、IO 冲高的情况。更多的需求背景再此不多介绍，总之，监控程序对 IO 的读写是极为重要的监控功能。

以 Linux 平台为例，我们可以使用工具监控到磁盘的 IO 读写情况、进程/线程的 IO 读写情况。但是有一个问题即为，有时候我们虽然定位了线程级别的 IO 问题，但是由于线程或许逻辑比较复杂，或者使用第三方库进行的 IO 操作。在排查 IO 问题时，不够直观，不能快速定位到问题。

因此有了此项目，接入此项目，可以实现文件级别的 IO 监控，也即可以反映出：

- 那个线程读了那个文件
- 文件名字
- IO 读/写的量（字节数）

后续，还可以提供更多的功能，比如在一个统计周期中不同的 IO 系统调用（open/close/read/write）的次数、是否存在文件描述符泄漏的情况等等

### 一、如何使用

```
mkdir build && cd build
cmake ..
make
make install
```
当执行完 `make install` 后，便会在当前目录下创建本项目的产出，名为 `file_io_hook` 的目录。

```
# tree file_io_hook 
file_io_hook
├── bin
│   └── example
├── include
│   ├── common.h
│   ├── concurrent_hash_map.h
│   ├── hook_io_handle.h
│   ├── io_hook.h
│   └── rw_spin_lock.h
└── lib
    ├── libdefault_hook.so
    └── libio_hook.so
```

可以先使用 example 例子程序，来观察效果

```
# 设置 LD_LIBRARY_PATH 环境变量，xxx 为执行 make 的目录
export LD_LIBRARY_PATH=xxx/file_io_hook/lib

cd file_io_hook/bin

# 正常执行 example 二进制，不 hook IO 系统调用
# ./example 
write file: test_01.txt success, write bytes: 14
read file: test_01.txt success, read bytes: 10
write file: test_02.txt success, write bytes: 14
read file: test_02.txt success, read bytes: 10
write file: test_03.txt success, write bytes: 14
read file: test_03.txt success, read bytes: 10

# hook IO 系统调用，执行 example 二进制
# LD_PRELOAD=../lib/libio_hook.so ./example
write file: test_01.txt success, write bytes: 14
call open for read failed, err: Permission denied
write file: test_02.txt success, write bytes: 14
call open for read failed, err: Permission denied
write file: test_03.txt success, write bytes: 14
call open for read failed, err: Permission denied
file r/w info: tid: 16099, name: test_02.txt, read(B): 0, write(B): 14
file r/w info: tid: 16099, name: test_03.txt, read(B): 0, write(B): 14
file r/w info: tid: 16099, name: test_01.txt, read(B): 0, write(B): 14
```

可以观察到当我们使用此项目时，可以监控到文件级别的 IO 操作。输出的信息包括：操作此文件的线程 id，被操作的文件名字，读 IO 量、写 IO 量

### 二、实现介绍

将文件 IO 函数进行 hook 拦截处理，在 IO 操作函数（open/close/read/write 等）中，加入业务逻辑

- open 函数，获取到被操作的文件名字和分配的文件描述符并且将其保存起来
- read/write 函数，通过文件描述符找到对应的文件信息，更新这个文件的信息
- close 函数，移除对应的文件信息

为了高效，实现了线程安全的“哈希表”、“读写自旋锁”等数据结构，用来支持性能。
采用双球模型来隔离读写线程要访问的临界区，提高性能。

代码比较简单，读者可以直接上手看代码。遇到的坑基本都写在注释中了。

### 三、后续

有问题欢迎提 issue。欢迎协同开发。欢迎技术交流。

