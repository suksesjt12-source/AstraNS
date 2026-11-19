#include "romfs.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace astrans {
namespace {

constexpr std::size_t kRomFsHeaderSize = 0x50U;
constexpr std::size_t kDirectoryEntryHeaderSize = 0x18U;
constexpr std::size_t kFileEntryHeaderSize = 0x20U;
constexpr std::size_t kMaximumNameLength = 4096U;

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

bool valid_range(uint64_t offset, uint64_t size, uint64_t total) noexcept {
    return offset <= total && size <= total - offset;
}

bool valid_optional_table(uint64_t offset, uint64_t size,
                          uint64_t total) noexcept {
    if (size == 0U) return offset <= total;
    return offset >= kRomFsHeaderSize && valid_range(offset, size, total);
}

bool valid_name(const uint8_t* data, std::size_t size) noexcept {
    for (std::size_t i = 0; i < size; ++i) {
        if (data[i] == 0U || data[i] == static_cast<uint8_t>('/') ||
            data[i] == static_cast<uint8_t>('\\')) return false;
    }
    return true;
}

bool split_path(std::string_view path, std::vector<std::string>& parts) {
    parts.clear();
    if (path.size() > 16384U) return false;
    if (path.find('\0') != std::string_view::npos ||
        path.find('\\') != std::string_view::npos) return false;

    std::size_t cursor = 0U;
    while (cursor < path.size()) {
        while (cursor < path.size() && path[cursor] == '/') ++cursor;
        const std::size_t start = cursor;
        while (cursor < path.size() && path[cursor] != '/') ++cursor;
        if (cursor == start) continue;
        const std::string_view part = path.substr(start, cursor - start);
        if (part == "." || part == ".." || part.size() > kMaximumNameLength)
            return false;
        parts.emplace_back(part);
    }
    return true;
}

}

bool RomFsImage::mount(const uint8_t* image, std::size_t image_size,
                       RomFsError& error) {
    unmount();
    error = RomFsError::None;
    if (image == nullptr) {
        error = RomFsError::NullInput;
        return false;
    }
    if (image_size < kRomFsHeaderSize) {
        error = RomFsError::TooSmall;
        return false;
    }
    if (std::memcmp(image, "IVFC", 4U) == 0) {
        error = RomFsError::UnsupportedIvfc;
        return false;
    }

    const uint64_t header_size = read_u64(image);
    const uint64_t directory_hash_offset = read_u64(image + 0x08U);
    const uint64_t directory_hash_size = read_u64(image + 0x10U);
    const uint64_t directory_metadata_offset = read_u64(image + 0x18U);
    const uint64_t directory_metadata_size = read_u64(image + 0x20U);
    const uint64_t file_hash_offset = read_u64(image + 0x28U);
    const uint64_t file_hash_size = read_u64(image + 0x30U);
    const uint64_t file_metadata_offset = read_u64(image + 0x38U);
    const uint64_t file_metadata_size = read_u64(image + 0x40U);
    const uint64_t file_data_offset = read_u64(image + 0x48U);
    const uint64_t total_size = static_cast<uint64_t>(image_size);

    if (header_size != kRomFsHeaderSize || directory_metadata_size < kDirectoryEntryHeaderSize ||
        file_data_offset > total_size) {
        error = RomFsError::InvalidHeader;
        return false;
    }
    if (!valid_optional_table(directory_hash_offset, directory_hash_size, total_size) ||
        !valid_optional_table(directory_metadata_offset, directory_metadata_size, total_size) ||
        !valid_optional_table(file_hash_offset, file_hash_size, total_size) ||
        !valid_optional_table(file_metadata_offset, file_metadata_size, total_size) ||
        directory_metadata_offset < header_size ||
        !valid_range(directory_metadata_offset, directory_metadata_size, total_size) ||
        (file_metadata_size != 0U &&
         !valid_range(file_metadata_offset, file_metadata_size, total_size))) {
        error = RomFsError::InvalidTableRange;
        return false;
    }

    storage_.assign(image, image + image_size);
    directory_metadata_offset_ = directory_metadata_offset;
    directory_metadata_size_ = directory_metadata_size;
    file_metadata_offset_ = file_metadata_offset;
    file_metadata_size_ = file_metadata_size;
    file_data_offset_ = file_data_offset;

    DirectoryRecord root{};
    if (!parse_directory(0U, root, error) || root.parent != kNoEntry ||
        !root.name.empty()) {
        unmount();
        if (error == RomFsError::None) error = RomFsError::InvalidEntry;
        return false;
    }

    mounted_ = true;
    return true;
}

void RomFsImage::unmount() noexcept {
    storage_.clear();
    directory_metadata_offset_ = 0U;
    directory_metadata_size_ = 0U;
    file_metadata_offset_ = 0U;
    file_metadata_size_ = 0U;
    file_data_offset_ = 0U;
    mounted_ = false;
}

bool RomFsImage::mounted() const noexcept {
    return mounted_;
}

uint64_t RomFsImage::image_size() const noexcept {
    return static_cast<uint64_t>(storage_.size());
}

bool RomFsImage::parse_directory(uint32_t offset, DirectoryRecord& record,
                                 RomFsError& error) const {
    record = {};
    error = RomFsError::None;
    if (!mounted_ && storage_.empty()) {
        error = RomFsError::InvalidEntry;
        return false;
    }
    if ((offset & 3U) != 0U || offset > directory_metadata_size_ ||
        directory_metadata_size_ - offset < kDirectoryEntryHeaderSize) {
        error = RomFsError::InvalidEntry;
        return false;
    }

    const uint8_t* entry = storage_.data() + directory_metadata_offset_ + offset;
    record.parent = read_u32(entry);
    record.sibling = read_u32(entry + 4U);
    record.child_directory = read_u32(entry + 8U);
    record.child_file = read_u32(entry + 12U);
    record.hash_sibling = read_u32(entry + 16U);
    const uint32_t name_size = read_u32(entry + 20U);
    if (name_size > kMaximumNameLength ||
        name_size > directory_metadata_size_ - offset - kDirectoryEntryHeaderSize ||
        (name_size != 0U && !valid_name(entry + kDirectoryEntryHeaderSize, name_size)) ||
        (record.sibling != kNoEntry &&
         ((record.sibling & 3U) != 0U || record.sibling >= directory_metadata_size_)) ||
        (record.child_directory != kNoEntry &&
         ((record.child_directory & 3U) != 0U ||
          record.child_directory >= directory_metadata_size_)) ||
        (record.child_file != kNoEntry &&
         ((record.child_file & 3U) != 0U ||
          record.child_file >= file_metadata_size_))) {
        error = RomFsError::InvalidEntry;
        return false;
    }
    if (name_size != 0U) {
        record.name.assign(reinterpret_cast<const char*>(entry + kDirectoryEntryHeaderSize),
                           name_size);
    }
    if (offset == 0U && name_size != 0U) {
        error = RomFsError::InvalidEntry;
        return false;
    }
    return true;
}

bool RomFsImage::parse_file(uint32_t offset, FileRecord& record,
                            RomFsError& error) const {
    record = {};
    error = RomFsError::None;
    if ((offset & 3U) != 0U || offset > file_metadata_size_ ||
        file_metadata_size_ - offset < kFileEntryHeaderSize) {
        error = RomFsError::InvalidEntry;
        return false;
    }

    const uint8_t* entry = storage_.data() + file_metadata_offset_ + offset;
    record.parent = read_u32(entry);
    record.sibling = read_u32(entry + 4U);
    record.data_offset = read_u64(entry + 8U);
    record.data_size = read_u64(entry + 16U);
    record.hash_sibling = read_u32(entry + 24U);
    const uint32_t name_size = read_u32(entry + 28U);
    const uint64_t table_remaining = file_metadata_size_ - offset;
    if (name_size > kMaximumNameLength ||
        name_size > table_remaining - kFileEntryHeaderSize ||
        (name_size != 0U && !valid_name(entry + kFileEntryHeaderSize, name_size)) ||
        (record.sibling != kNoEntry &&
         ((record.sibling & 3U) != 0U || record.sibling >= file_metadata_size_)) ||
        record.data_offset > std::numeric_limits<uint64_t>::max() - file_data_offset_ ||
        !valid_range(file_data_offset_ + record.data_offset, record.data_size,
                     static_cast<uint64_t>(storage_.size()))) {
        error = RomFsError::InvalidEntry;
        return false;
    }
    if (name_size == 0U) {
        error = RomFsError::InvalidEntry;
        return false;
    }
    record.name.assign(reinterpret_cast<const char*>(entry + kFileEntryHeaderSize),
                       name_size);
    return true;
}

bool RomFsImage::resolve_directory(std::string_view path, uint32_t& offset,
                                   RomFsError& error) const {
    offset = 0U;
    error = RomFsError::None;
    if (!mounted_) {
        error = RomFsError::InvalidHeader;
        return false;
    }

    std::vector<std::string> parts;
    if (!split_path(path, parts)) {
        error = RomFsError::InvalidPath;
        return false;
    }

    for (const std::string& part : parts) {
        DirectoryRecord parent{};
        if (!parse_directory(offset, parent, error)) return false;
        uint32_t cursor = parent.child_directory;
        std::set<uint32_t> visited;
        bool found = false;
        while (cursor != kNoEntry) {
            if (!visited.insert(cursor).second ||
                visited.size() > directory_metadata_size_ / kDirectoryEntryHeaderSize) {
                error = RomFsError::InvalidEntry;
                return false;
            }
            DirectoryRecord child{};
            if (!parse_directory(cursor, child, error)) return false;
            if (child.parent != offset) {
                error = RomFsError::InvalidEntry;
                return false;
            }
            if (child.name == part) {
                offset = cursor;
                found = true;
                break;
            }
            cursor = child.sibling;
        }
        if (!found) {
            error = RomFsError::NotFound;
            return false;
        }
    }
    return true;
}

bool RomFsImage::find_file(std::string_view path, FileRecord& record,
                           RomFsError& error) const {
    error = RomFsError::None;
    if (!mounted_) {
        error = RomFsError::InvalidHeader;
        return false;
    }
    if (path.empty() || path.size() > 16384U) {
        error = RomFsError::InvalidPath;
        return false;
    }

    const std::size_t slash = path.find_last_of('/');
    const std::string_view parent_path =
        slash == std::string_view::npos ? std::string_view{} : path.substr(0U, slash);
    const std::string_view leaf =
        slash == std::string_view::npos ? path : path.substr(slash + 1U);
    if (leaf.empty() || leaf == "." || leaf == ".." ||
        leaf.size() > kMaximumNameLength ||
        leaf.find('\0') != std::string_view::npos ||
        leaf.find('\\') != std::string_view::npos) {
        error = RomFsError::InvalidPath;
        return false;
    }

    uint32_t parent_offset = 0U;
    if (!resolve_directory(parent_path, parent_offset, error)) return false;
    DirectoryRecord parent{};
    if (!parse_directory(parent_offset, parent, error)) return false;

    uint32_t cursor = parent.child_file;
    std::set<uint32_t> visited;
    while (cursor != kNoEntry) {
        if (!visited.insert(cursor).second ||
            visited.size() > file_metadata_size_ / kFileEntryHeaderSize) {
            error = RomFsError::InvalidEntry;
            return false;
        }
        FileRecord candidate{};
        if (!parse_file(cursor, candidate, error)) return false;
        if (candidate.parent != parent_offset) {
            error = RomFsError::InvalidEntry;
            return false;
        }
        if (candidate.name == leaf) {
            record = std::move(candidate);
            return true;
        }
        cursor = candidate.sibling;
    }
    error = RomFsError::NotFound;
    return false;
}

bool RomFsImage::list_directory(std::string_view path,
                               std::vector<RomFsEntry>& entries,
                               RomFsError& error) const {
    entries.clear();
    error = RomFsError::None;
    uint32_t directory_offset = 0U;
    if (!resolve_directory(path, directory_offset, error)) return false;

    DirectoryRecord directory{};
    if (!parse_directory(directory_offset, directory, error)) return false;

    uint32_t cursor = directory.child_directory;
    std::set<uint32_t> visited_directories;
    while (cursor != kNoEntry) {
        if (!visited_directories.insert(cursor).second ||
            visited_directories.size() > kMaximumDirectoryEntries) {
            error = visited_directories.size() > kMaximumDirectoryEntries
                ? RomFsError::TooManyEntries : RomFsError::InvalidEntry;
            entries.clear();
            return false;
        }
        DirectoryRecord child{};
        if (!parse_directory(cursor, child, error) || child.parent != directory_offset) {
            if (error == RomFsError::None) error = RomFsError::InvalidEntry;
            entries.clear();
            return false;
        }
        entries.push_back({child.name, true, 0U});
        cursor = child.sibling;
    }

    cursor = directory.child_file;
    std::set<uint32_t> visited_files;
    while (cursor != kNoEntry) {
        if (!visited_files.insert(cursor).second ||
            visited_files.size() > kMaximumDirectoryEntries ||
            entries.size() >= kMaximumDirectoryEntries) {
            error = (visited_files.size() > kMaximumDirectoryEntries ||
                     entries.size() >= kMaximumDirectoryEntries)
                ? RomFsError::TooManyEntries : RomFsError::InvalidEntry;
            entries.clear();
            return false;
        }
        FileRecord file{};
        if (!parse_file(cursor, file, error) || file.parent != directory_offset) {
            if (error == RomFsError::None) error = RomFsError::InvalidEntry;
            entries.clear();
            return false;
        }
        entries.push_back({file.name, false, file.data_size});
        cursor = file.sibling;
    }

    std::sort(entries.begin(), entries.end(),
              [](const RomFsEntry& left, const RomFsEntry& right) {
                  if (left.is_directory != right.is_directory)
                      return left.is_directory > right.is_directory;
                  return left.name < right.name;
              });
    return true;
}

bool RomFsImage::get_file_size(std::string_view path, uint64_t& size,
                               RomFsError& error) const {
    size = 0U;
    FileRecord file{};
    if (!find_file(path, file, error)) return false;
    size = file.data_size;
    return true;
}

bool RomFsImage::read_file(std::string_view path, uint64_t offset,
                           std::size_t maximum_bytes,
                           std::vector<uint8_t>& output,
                           RomFsError& error) const {
    output.clear();
    error = RomFsError::None;
    if (maximum_bytes > kMaximumReadSize) {
        error = RomFsError::ReadLimitExceeded;
        return false;
    }

    FileRecord file{};
    if (!find_file(path, file, error)) return false;
    if (offset > file.data_size) {
        error = RomFsError::InvalidOffset;
        return false;
    }

    const uint64_t remaining = file.data_size - offset;
    const std::size_t amount = static_cast<std::size_t>(
        std::min<uint64_t>(remaining, static_cast<uint64_t>(maximum_bytes)));
    const uint64_t data_position = file_data_offset_ + file.data_offset + offset;
    if (!valid_range(data_position, amount, static_cast<uint64_t>(storage_.size()))) {
        error = RomFsError::InvalidEntry;
        return false;
    }
    output.assign(storage_.begin() + static_cast<std::ptrdiff_t>(data_position),
                  storage_.begin() + static_cast<std::ptrdiff_t>(data_position + amount));
    return true;
}

}
