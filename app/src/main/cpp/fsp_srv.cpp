#include "fsp_srv.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace astrans {
namespace {

constexpr std::size_t kMaximumPathLength = 4096U;
constexpr std::size_t kMaximumResponseSize = 1024U * 1024U;

uint32_t read_u32(const uint8_t* data) noexcept {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8U) |
           (static_cast<uint32_t>(data[2]) << 16U) |
           (static_cast<uint32_t>(data[3]) << 24U);
}

uint64_t read_u64(const uint8_t* data) noexcept {
    return static_cast<uint64_t>(read_u32(data)) |
           (static_cast<uint64_t>(read_u32(data + 4U)) << 32U);
}

void append_u16(std::vector<uint8_t>& output, uint16_t value) {
    output.push_back(static_cast<uint8_t>(value));
    output.push_back(static_cast<uint8_t>(value >> 8U));
}

void append_u32(std::vector<uint8_t>& output, uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i)
        output.push_back(static_cast<uint8_t>(value >> (i * 8U)));
}

void append_u64(std::vector<uint8_t>& output, uint64_t value) {
    for (unsigned i = 0U; i < 8U; ++i)
        output.push_back(static_cast<uint8_t>(value >> (i * 8U)));
}

bool read_path(const CmifRequest& request, std::string& path) {
    path.clear();
    if (request.parameters == nullptr || request.parameter_size < 4U) return false;
    const uint32_t length = read_u32(request.parameters);
    if (length == 0U || length > kMaximumPathLength ||
        request.parameter_size != 4U + static_cast<std::size_t>(length)) return false;
    const uint8_t* begin = request.parameters + 4U;
    for (std::size_t i = 0U; i < length; ++i) {
        if (begin[i] == 0U) return false;
    }
    path.assign(reinterpret_cast<const char*>(begin), length);
    return true;
}

FspResult translate_error(RomFsError error) noexcept {
    switch (error) {
        case RomFsError::None: return FspResult::Success;
        case RomFsError::NotFound: return FspResult::PathNotFound;
        case RomFsError::InvalidPath: return FspResult::InvalidPath;
        case RomFsError::ReadLimitExceeded: return FspResult::ReadLimitExceeded;
        case RomFsError::InvalidOffset: return FspResult::InvalidOffset;
        case RomFsError::NotDirectory:
        case RomFsError::IsDirectory: return FspResult::InvalidArgument;
        default: return FspResult::CorruptFilesystem;
    }
}

}

void FspSrv::set_image(std::shared_ptr<const RomFsImage> image) {
    image_ = std::move(image);
    file_handles_.clear();
    next_handle_ = 1U;
}

bool FspSrv::register_with(CmifServiceManager& services) {
    if (registered_) return true;
    registered_ = services.register_service(
        "fsp-srv",
        [this](const CmifRequest& request, std::vector<uint8_t>& payload) {
            return dispatch(request, payload);
        });
    return registered_;
}

uint32_t FspSrv::dispatch(const CmifRequest& request,
                          std::vector<uint8_t>& payload) {
    payload.clear();
    switch (static_cast<FspCommand>(request.method_id)) {
        case FspCommand::GetStatus:
            if (request.parameter_size != 0U) return static_cast<uint32_t>(FspResult::InvalidArgument);
            append_u32(payload, 1U);
            append_u32(payload, image_ && image_->mounted() ? 1U : 0U);
            append_u64(payload, image_ && image_->mounted() ? image_->image_size() : 0U);
            return static_cast<uint32_t>(FspResult::Success);
        case FspCommand::OpenFile:
            if (!image_ || !image_->mounted())
                return static_cast<uint32_t>(FspResult::NoRomFsMounted);
            return open_file(request, payload);
        case FspCommand::ReadFile:
            if (!image_ || !image_->mounted())
                return static_cast<uint32_t>(FspResult::NoRomFsMounted);
            return read_file(request, payload);
        case FspCommand::CloseFile:
            if (!image_ || !image_->mounted())
                return static_cast<uint32_t>(FspResult::NoRomFsMounted);
            return close_file(request);
        case FspCommand::ListDirectory:
            if (!image_ || !image_->mounted())
                return static_cast<uint32_t>(FspResult::NoRomFsMounted);
            return list_directory(request, payload);
    }
    return static_cast<uint32_t>(FspResult::UnsupportedCommand);
}

uint32_t FspSrv::open_file(const CmifRequest& request,
                           std::vector<uint8_t>& payload) {
    std::string path;
    if (!read_path(request, path))
        return static_cast<uint32_t>(FspResult::InvalidArgument);

    uint64_t file_size = 0U;
    RomFsError error = RomFsError::None;
    if (!image_->get_file_size(path, file_size, error))
        return static_cast<uint32_t>(translate_error(error));

    uint32_t handle = next_handle_;
    bool found = false;
    for (std::size_t attempt = 0U; attempt <= file_handles_.size(); ++attempt) {
        if (handle == 0U) handle = 1U;
        if (file_handles_.find(handle) == file_handles_.end()) {
            found = true;
            break;
        }
        ++handle;
    }
    if (!found) return static_cast<uint32_t>(FspResult::HandleExhausted);

    file_handles_.emplace(handle, std::move(path));
    next_handle_ = handle + 1U;
    if (next_handle_ == 0U) next_handle_ = 1U;
    append_u32(payload, handle);
    append_u64(payload, file_size);
    return static_cast<uint32_t>(FspResult::Success);
}

uint32_t FspSrv::read_file(const CmifRequest& request,
                           std::vector<uint8_t>& payload) {
    if (request.parameters == nullptr || request.parameter_size != 16U)
        return static_cast<uint32_t>(FspResult::InvalidArgument);

    const uint32_t handle = read_u32(request.parameters);
    const uint64_t offset = read_u64(request.parameters + 4U);
    const uint32_t maximum_bytes = read_u32(request.parameters + 12U);
    const auto file = file_handles_.find(handle);
    if (file == file_handles_.end())
        return static_cast<uint32_t>(FspResult::InvalidFileHandle);
    if (maximum_bytes > RomFsImage::kMaximumReadSize)
        return static_cast<uint32_t>(FspResult::ReadLimitExceeded);

    std::vector<uint8_t> bytes;
    RomFsError error = RomFsError::None;
    if (!image_->read_file(file->second, offset, maximum_bytes, bytes, error))
        return static_cast<uint32_t>(translate_error(error));
    if (bytes.size() > kMaximumResponseSize - 4U)
        return static_cast<uint32_t>(FspResult::ReadLimitExceeded);

    append_u32(payload, static_cast<uint32_t>(bytes.size()));
    payload.insert(payload.end(), bytes.begin(), bytes.end());
    return static_cast<uint32_t>(FspResult::Success);
}

uint32_t FspSrv::close_file(const CmifRequest& request) {
    if (request.parameters == nullptr || request.parameter_size != 4U)
        return static_cast<uint32_t>(FspResult::InvalidArgument);
    const uint32_t handle = read_u32(request.parameters);
    return file_handles_.erase(handle) == 1U
        ? static_cast<uint32_t>(FspResult::Success)
        : static_cast<uint32_t>(FspResult::InvalidFileHandle);
}

uint32_t FspSrv::list_directory(const CmifRequest& request,
                                std::vector<uint8_t>& payload) {
    std::string path;
    if (!read_path(request, path))
        return static_cast<uint32_t>(FspResult::InvalidArgument);

    std::vector<RomFsEntry> entries;
    RomFsError error = RomFsError::None;
    if (!image_->list_directory(path, entries, error))
        return static_cast<uint32_t>(translate_error(error));

    append_u32(payload, static_cast<uint32_t>(entries.size()));
    for (const RomFsEntry& entry : entries) {
        if (entry.name.size() > std::numeric_limits<uint16_t>::max() ||
            payload.size() > kMaximumResponseSize - 12U - entry.name.size()) {
            payload.clear();
            return static_cast<uint32_t>(FspResult::ReadLimitExceeded);
        }
        payload.push_back(entry.is_directory ? 1U : 0U);
        payload.push_back(0U);
        append_u16(payload, static_cast<uint16_t>(entry.name.size()));
        append_u64(payload, entry.size);
        payload.insert(payload.end(), entry.name.begin(), entry.name.end());
    }
    return static_cast<uint32_t>(FspResult::Success);
}

bool FspSrv::registered() const noexcept {
    return registered_;
}

std::size_t FspSrv::open_file_count() const noexcept {
    return file_handles_.size();
}

}
