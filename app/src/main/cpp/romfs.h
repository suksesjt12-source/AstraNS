#ifndef ASTRANS_ROMFS_H
#define ASTRANS_ROMFS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace astrans {

enum class RomFsError : uint8_t {
    None,
    NullInput,
    TooSmall,
    UnsupportedIvfc,
    InvalidHeader,
    InvalidTableRange,
    InvalidEntry,
    InvalidPath,
    NotFound,
    NotDirectory,
    IsDirectory,
    ReadLimitExceeded,
    InvalidOffset,
    TooManyEntries
};

struct RomFsEntry {
    std::string name;
    bool is_directory = false;
    uint64_t size = 0;
};

class RomFsImage {
public:
    static constexpr uint32_t kNoEntry = 0xFFFFFFFFU;
    static constexpr std::size_t kMaximumReadSize = 1024U * 1024U;
    static constexpr std::size_t kMaximumDirectoryEntries = 4096U;

    bool mount(const uint8_t* image, std::size_t image_size, RomFsError& error);
    void unmount() noexcept;
    bool mounted() const noexcept;
    uint64_t image_size() const noexcept;

    bool list_directory(std::string_view path, std::vector<RomFsEntry>& entries,
                        RomFsError& error) const;
    bool get_file_size(std::string_view path, uint64_t& size,
                       RomFsError& error) const;
    bool read_file(std::string_view path, uint64_t offset, std::size_t maximum_bytes,
                   std::vector<uint8_t>& output, RomFsError& error) const;

private:
    struct DirectoryRecord {
        uint32_t parent = kNoEntry;
        uint32_t sibling = kNoEntry;
        uint32_t child_directory = kNoEntry;
        uint32_t child_file = kNoEntry;
        uint32_t hash_sibling = kNoEntry;
        std::string name;
    };

    struct FileRecord {
        uint32_t parent = kNoEntry;
        uint32_t sibling = kNoEntry;
        uint64_t data_offset = 0;
        uint64_t data_size = 0;
        uint32_t hash_sibling = kNoEntry;
        std::string name;
    };

    bool parse_directory(uint32_t offset, DirectoryRecord& record,
                         RomFsError& error) const;
    bool parse_file(uint32_t offset, FileRecord& record,
                    RomFsError& error) const;
    bool resolve_directory(std::string_view path, uint32_t& offset,
                           RomFsError& error) const;
    bool find_file(std::string_view path, FileRecord& record,
                   RomFsError& error) const;

    std::vector<uint8_t> storage_;
    uint64_t directory_metadata_offset_ = 0;
    uint64_t directory_metadata_size_ = 0;
    uint64_t file_metadata_offset_ = 0;
    uint64_t file_metadata_size_ = 0;
    uint64_t file_data_offset_ = 0;
    bool mounted_ = false;
};

}

#endif
