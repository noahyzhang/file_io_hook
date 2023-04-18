#include <sstream>
#include "common/common.h"
#include "hook_io_handle.h"

namespace file_io_hook {

/**
 * @brief 为了进程退出时安全的退出
 * 
 */
namespace {
struct ProxyObjectExit {
    ProxyObjectExit() {
        atexit([]() {
            FileIoInfoHandler::get_instance().set_destruct_status();
        });
    }
};
static ProxyObjectExit g_dummy_obj;
}

void FileIoInfoHandler::add_hook_info(FileOperateType type, int fd, const char* file_name) {
    if (__glibc_unlikely(is_object_destruct)) {
        return;
    }
    if (__glibc_unlikely(type != FileOperateType::OPEN_TYPE && type != FileOperateType::CLOSE_TYPE)) {
        monitor_item.api_oc_param_error_num++;
        return;
    }
    if (fd < 0 || file_name == nullptr) {
        monitor_item.api_oc_param_error_num++;
        return;
    }
    switch (type) {
    case OPEN_TYPE:
        monitor_item.open_func_call_num++;
        fd_file_name_.insert(fd, std::string(file_name));
        break;
    case CLOSE_TYPE:
        monitor_item.close_func_call_num++;
        fd_file_name_.erase(fd);
        break;
    default:
        break;
    }
}

void FileIoInfoHandler::add_hook_info(FileOperateType type, int fd, size_t rw_size)  {
    if (__glibc_unlikely(is_object_destruct)) {
        return;
    }
    if (__glibc_unlikely(type != FileOperateType::READ_TYPE && type != FileOperateType::WRITE_TYPE)) {
        monitor_item.api_rw_param_error_num++;
        return;
    }
    if (data_pool_.size() > max_data_pool_size_) {
        monitor_item.exceed_data_pool_size_drop_num++;
        return;
    }
    auto tid = Util::get_tid();
    std::string file_name;
    if (!fd_file_name_.find(fd, file_name)) {
        monitor_item.not_found_fd_file_name_num++;
        return;
    }
    auto key = combine_key(tid, file_name);
    switch (type) {
    case READ_TYPE:
        monitor_item.read_func_call_num++;
        data_pool_.write(key, FileRWInfo{rw_size, 0});
        break;
    case WRITE_TYPE:
        monitor_item.write_func_call_num++;
        data_pool_.write(key, FileRWInfo{0, rw_size});
        break;
    default:
        break;
    }
}

const std::vector<FileInfo>& FileIoInfoHandler::consume_and_parse() {
    static std::vector<FileInfo> file_io_info_vec;
    file_io_info_vec.clear();
    if (__glibc_unlikely(is_object_destruct)) {
        return file_io_info_vec;
    }
    auto& io_data = data_pool_.read_and_switch();
    auto iter = io_data.get_iterator();
    uint64_t tid = 0;
    std::string file_name;
    for (; iter != nullptr; iter++) {
        if (divide_key(iter->get_key(), &tid, &file_name) < 0) {
            continue;
        }
        file_io_info_vec.emplace_back(FileInfo{
            .tid = tid,
            .file_name = file_name,
            .read_b = iter->get_value().read_b,
            .write_b = iter->get_value().write_b});
    }
    // 按照读写数据量进行降序排序
    std::sort(file_io_info_vec.begin(), file_io_info_vec.end(),
        [](const FileInfo& left, const FileInfo& right) {
        return left.read_b + left.write_b > right.read_b + right.write_b;
    });
    // std::cout << "FileIoInfoHandler::consume_and_parse, IO-hook monitor item: "
    //     << "open func call num: " << monitor_item.open_func_call_num.exchange(0)
    //     << ", close func call num: " << monitor_item.close_func_call_num.exchange(0)
    //     << ", read func call num: " << monitor_item.read_func_call_num.exchange(0)
    //     << ", write func call num: " << monitor_item.write_func_call_num.exchange(0)
    //     << ", open/close api param error num: " << monitor_item.api_oc_param_error_num.exchange(0)
    //     << ", read/write api param error num: " << monitor_item.api_rw_param_error_num.exchange(0)
    //     << ", exceed data pool size generate drop num: " << monitor_item.exceed_data_pool_size_drop_num.exchange(0)
    //     << ", not found fd-file_name num: " << monitor_item.not_found_fd_file_name_num.exchange(0);
    return file_io_info_vec;
}

std::string FileIoInfoHandler::combine_key(uint64_t tid, const std::string& file_name) {
    std::stringstream oss;
    oss << tid << SEPARATOR_CHAR << file_name;
    return oss.str();
}

int FileIoInfoHandler::divide_key(const std::string& key, uint64_t* tid, std::string* file_name) {
    const auto begin = key.begin();
    const auto end = key.end();
    const auto next = std::find_if(begin, end, [&](char c) {return c == SEPARATOR_CHAR; });
    if (next == end) {
        return -1;
    }
    const auto token = std::string(begin, next);
    *tid = std::atoi(token.c_str());
    *file_name = std::string(next+1, end);
    return 0;
}

}  // namespace file_io_hook

