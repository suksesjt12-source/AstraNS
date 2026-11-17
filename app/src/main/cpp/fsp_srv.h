#ifndef ASTRANS_FSP_SRV_H
#define ASTRANS_FSP_SRV_H

#include "hipc_cmif.h"
#include "romfs.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace astrans {

enum class FspResult : uint32_t {
    Success = 0,
    InvalidArgument = 1,
    NoRomFsMounted = 2,
    PathNotFound = 3,
    InvalidPath = 4,
    InvalidFileHandle = 5,
    ReadLimitExceeded = 6,
    InvalidOffset = 7,
    CorruptFilesystem = 8,
    UnsupportedCommand = 9,
    HandleExhausted = 10
};

enum class FspCommand : uint32_t {
    GetStatus = 0,
    OpenFile = 1,
    ReadFile = 2,
    CloseFile = 3,
    ListDirectory = 4
};

class FspSrv {
public:
    FspSrv() = default;

    void set_image(std::shared_ptr<const RomFsImage> image);
    bool register_with(CmifServiceManager& services);
    uint32_t dispatch(const CmifRequest& request,
                      std::vector<uint8_t>& payload);

    bool registered() const noexcept;
    std::size_t open_file_count() const noexcept;

private:
    uint32_t open_file(const CmifRequest& request,
                       std::vector<uint8_t>& payload);
    uint32_t read_file(const CmifRequest& request,
                       std::vector<uint8_t>& payload);
    uint32_t close_file(const CmifRequest& request);
    uint32_t list_directory(const CmifRequest& request,
                            std::vector<uint8_t>& payload);

    std::shared_ptr<const RomFsImage> image_;
    std::map<uint32_t, std::string> file_handles_;
    uint32_t next_handle_ = 1U;
    bool registered_ = false;
};

}

#endif
