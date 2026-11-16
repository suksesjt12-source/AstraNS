#include "hipc_cmif.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace astrans {
namespace {

constexpr std::size_t kHipcHeaderSize = 8U;
constexpr std::size_t kSpecialHeaderSize = 4U;
constexpr std::size_t kProcessIdSize = 8U;
constexpr std::size_t kHandleSize = 4U;
constexpr std::size_t kPointerDescriptorSize = 8U;
constexpr std::size_t kMapDescriptorSize = 12U;
constexpr std::size_t kCmifHeaderSize = 16U;
constexpr uint32_t kSfci = 0x49434653U;
constexpr uint32_t kSfco = 0x4F434653U;

uint32_t read_u32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8U) |
           (static_cast<uint32_t>(p[2]) << 16U) |
           (static_cast<uint32_t>(p[3]) << 24U);
}
uint64_t read_u64(const uint8_t* p) noexcept {
    return static_cast<uint64_t>(read_u32(p)) |
           (static_cast<uint64_t>(read_u32(p + 4U)) << 32U);
}
void write_u16(uint8_t* p, uint16_t value) noexcept {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8U);
}
void write_u32(uint8_t* p, uint32_t value) noexcept {
    for (unsigned i = 0; i < 4U; ++i)
        p[i] = static_cast<uint8_t>(value >> (i * 8U));
}
bool add_size(std::size_t a, std::size_t b, std::size_t& result) noexcept {
    if (a > std::numeric_limits<std::size_t>::max() - b) return false;
    result = a + b;
    return true;
}
bool align_up_16(std::size_t value, std::size_t& aligned) noexcept {
    if (value > std::numeric_limits<std::size_t>::max() - 15U) return false;
    aligned = (value + 15U) & ~std::size_t{15U};
    return true;
}

}  // namespace

bool parse_hipc_message(const uint8_t* data, std::size_t size,
                        HipcMessage& message, HipcError& error) {
    message = {};
    error = HipcError::None;
    if (data == nullptr) {
        error = HipcError::NullInput;
        return false;
    }
    if (size < kHipcHeaderSize) {
        error = HipcError::TruncatedHeader;
        return false;
    }

    const uint32_t header0 = read_u32(data);
    const uint32_t header1 = read_u32(data + 4U);
    message.message_type = static_cast<uint16_t>(header0 & 0xFFFFU);
    message.pointer_count = static_cast<uint8_t>((header0 >> 16U) & 0xFU);
    message.send_count = static_cast<uint8_t>((header0 >> 20U) & 0xFU);
    message.receive_count = static_cast<uint8_t>((header0 >> 24U) & 0xFU);
    message.exchange_count = static_cast<uint8_t>((header0 >> 28U) & 0xFU);
    const std::size_t raw_words = header1 & 0x3FFU;
    message.receive_list_count = static_cast<uint8_t>((header1 >> 10U) & 0xFU);
    message.receive_list_offset = static_cast<uint16_t>((header1 >> 20U) & 0x7FFU);
    const bool has_special_header = (header1 & 0x80000000U) != 0U;

    std::size_t cursor = kHipcHeaderSize;
    if (has_special_header) {
        if (size - cursor < kSpecialHeaderSize) {
            error = HipcError::TruncatedSpecialHeader;
            return false;
        }
        const uint32_t special = read_u32(data + cursor);
        cursor += kSpecialHeaderSize;
        message.has_process_id = (special & 1U) != 0U;
        const std::size_t copy_count = (special >> 1U) & 0xFU;
        const std::size_t move_count = (special >> 5U) & 0xFU;

        if (message.has_process_id) {
            if (size - cursor < kProcessIdSize) {
                error = HipcError::TruncatedSpecialHeader;
                return false;
            }
            message.process_id = read_u64(data + cursor);
            cursor += kProcessIdSize;
        }
        std::size_t all_handles = 0U;
        if (!add_size(copy_count, move_count, all_handles) ||
            all_handles > (size - cursor) / kHandleSize) {
            error = HipcError::TruncatedHandles;
            return false;
        }
        message.copy_handles.reserve(copy_count);
        message.move_handles.reserve(move_count);
        for (std::size_t i = 0; i < copy_count; ++i) {
            message.copy_handles.push_back(read_u32(data + cursor));
            cursor += kHandleSize;
        }
        for (std::size_t i = 0; i < move_count; ++i) {
            message.move_handles.push_back(read_u32(data + cursor));
            cursor += kHandleSize;
        }
    }

    std::size_t descriptor_bytes = 0U;
    const std::size_t pointer_bytes =
        static_cast<std::size_t>(message.pointer_count) * kPointerDescriptorSize;
    const std::size_t map_count =
        static_cast<std::size_t>(message.send_count) + message.receive_count +
        message.exchange_count;
    if (map_count > std::numeric_limits<std::size_t>::max() / kMapDescriptorSize) {
        error = HipcError::TruncatedDescriptors;
        return false;
    }
    const std::size_t map_bytes = map_count * kMapDescriptorSize;
    if (!add_size(pointer_bytes, map_bytes, descriptor_bytes) ||
        descriptor_bytes > size - cursor) {
        error = HipcError::TruncatedDescriptors;
        return false;
    }
    cursor += descriptor_bytes;

    std::size_t raw_offset = 0U;
    if (!align_up_16(cursor, raw_offset) || raw_offset > size) {
        error = HipcError::TruncatedRawData;
        return false;
    }
    const std::size_t raw_size = raw_words * 4U;
    if (raw_size > size - raw_offset) {
        error = HipcError::TruncatedRawData;
        return false;
    }
    message.raw_data_offset = raw_offset;
    message.raw_data_size = raw_size;
    return true;
}

bool parse_cmif_request(const uint8_t* data, std::size_t size,
                        const HipcMessage& message, CmifRequest& request,
                        HipcError& error) {
    request = {};
    error = HipcError::None;
    if (data == nullptr) {
        error = HipcError::NullInput;
        return false;
    }
    if (message.raw_data_offset > size ||
        message.raw_data_size > size - message.raw_data_offset) {
        error = HipcError::TruncatedRawData;
        return false;
    }

    const uint8_t* raw = data + message.raw_data_offset;
    std::size_t cmif_offset = message.raw_data_size;
    // CMIF permits alignment padding before the 16-byte command header.
    const std::size_t scan_limit = std::min<std::size_t>(16U, message.raw_data_size);
    for (std::size_t offset = 0U; offset + kCmifHeaderSize <= scan_limit; offset += 4U) {
        if (read_u32(raw + offset) == kSfci) {
            cmif_offset = offset;
            break;
        }
    }
    if (cmif_offset == message.raw_data_size) {
        error = HipcError::InvalidCmifSignature;
        return false;
    }

    const uint16_t version = static_cast<uint16_t>(raw[cmif_offset + 4U]) |
                             static_cast<uint16_t>(raw[cmif_offset + 5U] << 8U);
    if (version > 1U) {
        error = HipcError::InvalidCmifVersion;
        return false;
    }
    request.version = version;
    request.method_id = read_u32(raw + cmif_offset + 8U);
    request.token = read_u32(raw + cmif_offset + 12U);
    request.header_offset = message.raw_data_offset + cmif_offset;
    request.parameters = raw + cmif_offset + kCmifHeaderSize;
    request.parameter_size = message.raw_data_size - cmif_offset - kCmifHeaderSize;
    return true;
}

bool build_cmif_response(uint16_t version, uint32_t result,
                         uint32_t interface_id,
                         const uint8_t* payload, std::size_t payload_size,
                         std::vector<uint8_t>& output, HipcError& error) {
    output.clear();
    error = HipcError::None;
    if (payload == nullptr && payload_size != 0U) {
        error = HipcError::InvalidOutputBuffer;
        return false;
    }
    if (version > 1U || payload_size > std::numeric_limits<std::size_t>::max() -
                                           kCmifHeaderSize) {
        error = HipcError::InvalidOutputBuffer;
        return false;
    }
    output.resize(kCmifHeaderSize + payload_size, 0U);
    write_u32(output.data(), kSfco);
    write_u16(output.data() + 4U, version);
    write_u32(output.data() + 8U, result);
    write_u32(output.data() + 12U, interface_id);
    if (payload_size != 0U)
        std::copy(payload, payload + payload_size, output.begin() + kCmifHeaderSize);
    return true;
}


bool parse_cmif_domain_request(const uint8_t* data, std::size_t size,
                               const HipcMessage& message,
                               CmifDomainRequest& request, HipcError& error) {
    request = {};
    error = HipcError::None;
    if (data == nullptr) {
        error = HipcError::NullInput;
        return false;
    }
    if (message.raw_data_offset > size ||
        message.raw_data_size > size - message.raw_data_offset ||
        message.raw_data_size < 16U) {
        error = HipcError::TruncatedRawData;
        return false;
    }

    const uint8_t* raw = data + message.raw_data_offset;
    const uint8_t request_kind = raw[0];
    const uint8_t object_count = raw[1];
    const uint16_t cmif_raw_size = static_cast<uint16_t>(raw[2]) |
                                   static_cast<uint16_t>(raw[3] << 8U);
    const uint32_t target_object_id = read_u32(raw + 4U);
    const uint32_t token = read_u32(raw + 12U);
    if (request_kind != 1U && request_kind != 2U) {
        error = HipcError::InvalidCmifSignature;
        return false;
    }
    if (cmif_raw_size < kCmifHeaderSize || cmif_raw_size > message.raw_data_size - 16U) {
        error = HipcError::TruncatedRawData;
        return false;
    }

    HipcMessage nested = message;
    nested.raw_data_offset = message.raw_data_offset + 16U;
    nested.raw_data_size = cmif_raw_size;
    if (!parse_cmif_request(data, size, nested, request.command, error)) return false;

    const std::size_t object_ids_offset = 16U + cmif_raw_size;
    const std::size_t object_id_bytes = static_cast<std::size_t>(object_count) * 4U;
    if (object_ids_offset > message.raw_data_size ||
        object_id_bytes > message.raw_data_size - object_ids_offset) {
        error = HipcError::TruncatedRawData;
        return false;
    }

    request.request_kind = request_kind;
    request.target_object_id = target_object_id;
    request.token = token;
    request.input_object_ids.reserve(object_count);
    for (std::size_t i = 0; i < object_count; ++i)
        request.input_object_ids.push_back(read_u32(raw + object_ids_offset + i * 4U));
    return true;
}

bool build_cmif_domain_response(uint32_t output_object_count,
                                const uint8_t* cmif_payload,
                                std::size_t cmif_payload_size,
                                std::vector<uint8_t>& output,
                                HipcError& error) {
    output.clear();
    error = HipcError::None;
    if (cmif_payload == nullptr && cmif_payload_size != 0U) {
        error = HipcError::InvalidOutputBuffer;
        return false;
    }
    if (cmif_payload_size > std::numeric_limits<std::size_t>::max() - 16U) {
        error = HipcError::InvalidOutputBuffer;
        return false;
    }
    output.resize(16U + cmif_payload_size, 0U);
    write_u32(output.data(), output_object_count);
    if (cmif_payload_size != 0U)
        std::copy(cmif_payload, cmif_payload + cmif_payload_size, output.begin() + 16U);
    return true;
}

uint32_t IpcHandleTable::insert(std::shared_ptr<IpcKernelObject> object) {
    if (!object || next_handle_ == 0U) return 0U;
    const uint32_t handle = next_handle_++;
    handles_.emplace(handle, std::move(object));
    return handle;
}

uint32_t IpcHandleTable::duplicate(uint32_t handle) {
    const auto it = handles_.find(handle);
    if (it == handles_.end()) return 0U;
    return insert(it->second);
}

uint32_t IpcHandleTable::move(uint32_t handle) {
    const auto it = handles_.find(handle);
    if (it == handles_.end()) return 0U;
    auto object = std::move(it->second);
    handles_.erase(it);
    const uint32_t new_handle = insert(std::move(object));
    return new_handle;
}

bool IpcHandleTable::close(uint32_t handle) {
    return handles_.erase(handle) == 1U;
}

std::shared_ptr<IpcKernelObject> IpcHandleTable::get(uint32_t handle) const {
    const auto it = handles_.find(handle);
    return it == handles_.end() ? nullptr : it->second;
}

std::size_t IpcHandleTable::size() const noexcept { return handles_.size(); }

uint32_t CmifDomainTable::insert(CmifServiceHandler handler) {
    if (!handler || next_object_id_ == 0U) return 0U;
    const uint32_t object_id = next_object_id_++;
    objects_.emplace(object_id, std::move(handler));
    return object_id;
}

bool CmifDomainTable::remove(uint32_t object_id) {
    return objects_.erase(object_id) == 1U;
}

const CmifServiceHandler* CmifDomainTable::get(uint32_t object_id) const noexcept {
    const auto it = objects_.find(object_id);
    return it == objects_.end() ? nullptr : &it->second;
}

std::size_t CmifDomainTable::size() const noexcept { return objects_.size(); }


bool parse_tipc_request(const uint8_t* data, std::size_t size,
                        const HipcMessage& message,
                        TipcRequest& request, HipcError& error) {
    request = {};
    error = HipcError::None;
    if (data == nullptr) {
        error = HipcError::NullInput;
        return false;
    }
    if (message.raw_data_offset > size ||
        message.raw_data_size > size - message.raw_data_offset) {
        error = HipcError::TruncatedRawData;
        return false;
    }
    request.command_id = message.message_type;
    request.payload = data + message.raw_data_offset;
    request.payload_size = message.raw_data_size;
    return true;
}

bool encode_service_name(const std::string& name,
                         std::array<uint8_t, 8>& encoded) noexcept {
    encoded.fill(0U);
    if (name.empty() || name.size() > encoded.size()) return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char value = static_cast<unsigned char>(name[i]);
        if (value == 0U || value > 0x7FU) return false;
        encoded[i] = static_cast<uint8_t>(value);
    }
    return true;
}

bool decode_service_name(const uint8_t* encoded, std::size_t size,
                         std::string& name) noexcept {
    name.clear();
    if (encoded == nullptr || size != 8U) return false;
    bool saw_zero = false;
    for (std::size_t i = 0; i < 8U; ++i) {
        if (encoded[i] == 0U) {
            saw_zero = true;
            continue;
        }
        if (saw_zero || encoded[i] > 0x7FU) {
            name.clear();
            return false;
        }
        name.push_back(static_cast<char>(encoded[i]));
    }
    return !name.empty();
}

bool SmServiceRegistry::register_service(const std::string& name, bool is_light,
                                          int32_t max_sessions) {
    std::array<uint8_t, 8> encoded{};
    if (!encode_service_name(name, encoded) || max_sessions <= 0 ||
        services_.find(name) != services_.end()) return false;
    services_.emplace(name, Entry{is_light, max_sessions, 0});
    return true;
}

bool SmServiceRegistry::unregister_service(const std::string& name) {
    const auto it = services_.find(name);
    if (it == services_.end() || it->second.active_sessions != 0) return false;
    services_.erase(it);
    return true;
}

bool SmServiceRegistry::get_service_handle(const std::string& name,
                                            IpcHandleTable& handles,
                                            uint32_t& handle) {
    handle = 0U;
    const auto it = services_.find(name);
    if (it == services_.end() ||
        it->second.active_sessions >= it->second.max_sessions) return false;
    auto session = std::make_shared<IpcKernelObject>(IpcObjectType::Session);
    const uint32_t created = handles.insert(std::move(session));
    if (created == 0U) return false;
    session_handles_.emplace(created, name);
    ++it->second.active_sessions;
    handle = created;
    return true;
}

bool SmServiceRegistry::close_service_handle(uint32_t handle,
                                              IpcHandleTable& handles) {
    const auto session = session_handles_.find(handle);
    if (session == session_handles_.end()) return false;
    const auto service = services_.find(session->second);
    if (service == services_.end() || service->second.active_sessions <= 0) return false;
    if (!handles.close(handle)) return false;
    --service->second.active_sessions;
    session_handles_.erase(session);
    return true;
}

std::size_t SmServiceRegistry::service_count() const noexcept {
    return services_.size();
}

std::size_t SmServiceRegistry::active_session_count(const std::string& name) const noexcept {
    const auto it = services_.find(name);
    return it == services_.end() ? 0U :
        static_cast<std::size_t>(it->second.active_sessions);
}

bool CmifServiceManager::register_service(const std::string& name,
                                           CmifServiceHandler handler) {
    if (name.empty() || !handler || services_.find(name) != services_.end()) return false;
    services_.emplace(name, std::move(handler));
    return true;
}

bool CmifServiceManager::dispatch(const std::string& name, const CmifRequest& request,
                                  std::vector<uint8_t>& response,
                                  HipcError& error) const {
    const auto service = services_.find(name);
    if (service == services_.end()) {
        response.clear();
        error = HipcError::ServiceNotFound;
        return false;
    }
    std::vector<uint8_t> payload;
    const uint32_t result = service->second(request, payload);
    return build_cmif_response(request.version, result, 0U,
                               payload.data(), payload.size(), response, error);
}

std::size_t CmifServiceManager::service_count() const noexcept {
    return services_.size();
}

}  // namespace astrans
