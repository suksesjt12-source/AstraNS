#include "fsp_srv.h"
#include "nro_assets.h"
#include "romfs.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}

void write_u32(std::vector<uint8_t>& data, std::size_t offset, uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i)
        data[offset + i] = static_cast<uint8_t>(value >> (i * 8U));
}

void write_u64(std::vector<uint8_t>& data, std::size_t offset, uint64_t value) {
    for (unsigned i = 0U; i < 8U; ++i)
        data[offset + i] = static_cast<uint8_t>(value >> (i * 8U));
}

uint32_t read_u32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8U) |
           (static_cast<uint32_t>(data[2]) << 16U) |
           (static_cast<uint32_t>(data[3]) << 24U);
}

uint64_t read_u64(const uint8_t* data) {
    return static_cast<uint64_t>(read_u32(data)) |
           (static_cast<uint64_t>(read_u32(data + 4U)) << 32U);
}

std::vector<uint8_t> make_romfs() {
    const std::string file_contents = "AstraNS Stage 4";
    std::vector<uint8_t> image(0x94U + file_contents.size(), 0U);
    write_u64(image, 0x00U, 0x50U);
    write_u64(image, 0x08U, 0x50U);
    write_u64(image, 0x10U, 0x04U);
    write_u64(image, 0x18U, 0x54U);
    write_u64(image, 0x20U, 0x18U);
    write_u64(image, 0x28U, 0x6CU);
    write_u64(image, 0x30U, 0x04U);
    write_u64(image, 0x38U, 0x70U);
    write_u64(image, 0x40U, 0x24U);
    write_u64(image, 0x48U, 0x94U);

    write_u32(image, 0x54U, 0xFFFFFFFFU);
    write_u32(image, 0x58U, 0xFFFFFFFFU);
    write_u32(image, 0x5CU, 0xFFFFFFFFU);
    write_u32(image, 0x60U, 0U);
    write_u32(image, 0x64U, 0xFFFFFFFFU);
    write_u32(image, 0x68U, 0U);

    write_u32(image, 0x70U, 0U);
    write_u32(image, 0x74U, 0xFFFFFFFFU);
    write_u64(image, 0x78U, 0U);
    write_u64(image, 0x80U, file_contents.size());
    write_u32(image, 0x88U, 0xFFFFFFFFU);
    write_u32(image, 0x8CU, 4U);
    image[0x90U] = 'd';
    image[0x91U] = 'a';
    image[0x92U] = 't';
    image[0x93U] = 'a';

    for (std::size_t i = 0U; i < file_contents.size(); ++i)
        image[0x94U + i] = static_cast<uint8_t>(file_contents[i]);
    return image;
}

std::vector<uint8_t> make_nro(const std::vector<uint8_t>& romfs) {
    const std::size_t asset_start = 0x80U;
    std::vector<uint8_t> nro(asset_start + 0x38U + romfs.size(), 0U);
    std::memcpy(nro.data() + 0x10U, "NRO0", 4U);
    write_u32(nro, 0x18U, static_cast<uint32_t>(asset_start));
    std::memcpy(nro.data() + asset_start, "ASET", 4U);
    write_u32(nro, asset_start + 4U, 0U);
    write_u64(nro, asset_start + 0x28U, 0x38U);
    write_u64(nro, asset_start + 0x30U, romfs.size());
    std::copy(romfs.begin(), romfs.end(), nro.begin() + asset_start + 0x38U);
    return nro;
}

std::vector<uint8_t> path_parameter(const std::string& path) {
    std::vector<uint8_t> result(4U + path.size(), 0U);
    write_u32(result, 0U, static_cast<uint32_t>(path.size()));
    std::copy(path.begin(), path.end(), result.begin() + 4U);
    return result;
}

}

int main() {
    const std::vector<uint8_t> romfs_data = make_romfs();
    const std::vector<uint8_t> nro_data = make_nro(romfs_data);

    std::vector<uint8_t> extracted;
    astrans::NroAssetInfo asset_info{};
    astrans::NroAssetError asset_error = astrans::NroAssetError::None;
    check(astrans::extract_nro_romfs(nro_data.data(), nro_data.size(),
                                     extracted, asset_info, asset_error),
          "extract RomFS from NRO ASET section");
    check(asset_error == astrans::NroAssetError::None &&
          extracted == romfs_data && asset_info.romfs_offset == 0x38U,
          "ASET-relative RomFS range is copied exactly");

    auto image = std::make_shared<astrans::RomFsImage>();
    astrans::RomFsError romfs_error = astrans::RomFsError::None;
    check(image->mount(extracted.data(), extracted.size(), romfs_error),
          "mount raw Switch RomFS image");
    check(image->mounted() && image->image_size() == romfs_data.size(),
          "RomFS reports mounted image size");

    std::vector<astrans::RomFsEntry> entries;
    check(image->list_directory("/", entries, romfs_error) && entries.size() == 1U &&
          entries[0].name == "data" && !entries[0].is_directory &&
          entries[0].size == std::string("AstraNS Stage 4").size(),
          "list root directory metadata");

    std::vector<uint8_t> contents;
    check(image->read_file("/data", 0U, 128U, contents, romfs_error) &&
          std::string(contents.begin(), contents.end()) == "AstraNS Stage 4",
          "read a file through RomFS metadata");
    check(!image->read_file("/../data", 0U, 128U, contents, romfs_error) &&
          romfs_error == astrans::RomFsError::InvalidPath,
          "reject parent traversal in RomFS paths");
    check(!image->read_file("/data", 0U,
                            astrans::RomFsImage::kMaximumReadSize + 1U,
                            contents, romfs_error) &&
          romfs_error == astrans::RomFsError::ReadLimitExceeded,
          "enforce bounded RomFS read size");

    astrans::CmifServiceManager services;
    astrans::FspSrv fsp;
    fsp.set_image(image);
    check(fsp.register_with(services) && fsp.registered(),
          "register fsp-srv with CMIF service manager");

    const std::vector<uint8_t> open_parameters = path_parameter("/data");
    astrans::CmifRequest request{};
    request.method_id = static_cast<uint32_t>(astrans::FspCommand::OpenFile);
    request.parameters = open_parameters.data();
    request.parameter_size = open_parameters.size();

    std::vector<uint8_t> response;
    astrans::HipcError ipc_error = astrans::HipcError::None;
    check(services.dispatch("fsp-srv", request, response, ipc_error),
          "dispatch fsp-srv OpenFile command");
    check(response.size() == 28U && read_u32(response.data() + 8U) == 0U,
          "OpenFile returns a successful CMIF response");
    const uint32_t handle = read_u32(response.data() + 16U);
    check(handle != 0U && read_u64(response.data() + 20U) ==
          std::string("AstraNS Stage 4").size(),
          "OpenFile returns a file handle and size");

    std::vector<uint8_t> read_parameters(16U, 0U);
    write_u32(read_parameters, 0U, handle);
    write_u64(read_parameters, 4U, 0U);
    write_u32(read_parameters, 12U, 128U);
    request.method_id = static_cast<uint32_t>(astrans::FspCommand::ReadFile);
    request.parameters = read_parameters.data();
    request.parameter_size = read_parameters.size();
    check(services.dispatch("fsp-srv", request, response, ipc_error),
          "dispatch fsp-srv ReadFile command");
    check(response.size() == 16U + 4U + std::string("AstraNS Stage 4").size() &&
          read_u32(response.data() + 16U) == std::string("AstraNS Stage 4").size() &&
          std::string(response.begin() + 20, response.end()) == "AstraNS Stage 4",
          "ReadFile returns bounded data bytes");

    std::vector<uint8_t> close_parameters(4U, 0U);
    write_u32(close_parameters, 0U, handle);
    request.method_id = static_cast<uint32_t>(astrans::FspCommand::CloseFile);
    request.parameters = close_parameters.data();
    request.parameter_size = close_parameters.size();
    check(services.dispatch("fsp-srv", request, response, ipc_error),
          "dispatch fsp-srv CloseFile command");
    check(fsp.open_file_count() == 0U, "CloseFile releases its session handle");

    std::vector<uint8_t> invalid = romfs_data;
    write_u64(invalid, 0x18U, UINT64_MAX);
    astrans::RomFsImage rejected;
    check(!rejected.mount(invalid.data(), invalid.size(), romfs_error) &&
          romfs_error == astrans::RomFsError::InvalidTableRange,
          "reject invalid RomFS metadata table range");

    if (failures != 0) {
        std::cerr << failures << " filesystem tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "AstraNS Stage 4 RomFS and fsp-srv tests: PASS\n";
    return EXIT_SUCCESS;
}
