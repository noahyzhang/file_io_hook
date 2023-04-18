#include "hook_io_handle.h"

namespace file_io_hook {

void FileIoInfoHandler::add_hook_info(FileOperateType, int, const char*) {
    return;
}

void FileIoInfoHandler::add_hook_info(FileOperateType, int, size_t) {
    return;
}

const std::vector<FileInfo>& FileIoInfoHandler::consume_and_parse() {
    static std::vector<FileInfo> dummy;
    return dummy;
}

}  // namespace file_io_hook
