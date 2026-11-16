#ifndef ASTRANS_HIPC_CMIF_H
#define ASTRANS_HIPC_CMIF_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace astrans {

enum class HipcError : uint8_t {
    None,
    NullInput,
    TruncatedHeader,
    TruncatedSpecialHeader,
    TruncatedHandles,
    TruncatedDescriptors,
    TruncatedRawData,
    InvalidCmifSignature,
    InvalidCmifVersion,
    ServiceNotFound,
    InvalidOutputBuffer,
};

struct HipcMessage {
    uint16_t message_type = 0;
    uint8_t pointer_count = 0;
    uint8_t send_count = 0;
    uint8_t receive_count = 0;
    uint8_t exchange_count = 0;
    uint8_t receive_list_count = 0;
    uint16_t receive_list_offset = 0;
    bool has_process_id = false;
    uint64_t process_id = 0;
    std::vector<uint32_t> copy_handles;
    std::vector<uint32_t> move_handles;
    std::size_t raw_data_offset = 0;
    std::size_t raw_data_size = 0;
};

struct CmifRequest {
    uint16_t version = 0;
    uint32_t method_id = 0;
    uint32_t token = 0;
    std::size_t header_offset = 0;
    const uint8_t* parameters = nullptr;
    std::size_t parameter_size = 0;
};

bool parse_hipc_message(const uint8_t* data, std::size_t size,
                        HipcMessage& message, HipcError& error);
bool parse_cmif_request(const uint8_t* data, std::size_t size,
                        const HipcMessage& message, CmifRequest& request,
                        HipcError& error);

bool build_cmif_response(uint16_t version, uint32_t result,
                         uint32_t interface_id,
                         const uint8_t* payload, std::size_t payload_size,
                         std::vector<uint8_t>& output,
                         HipcError& error);

using CmifServiceHandler =
    std::function<uint32_t(const CmifRequest&, std::vector<uint8_t>&)>;

class CmifServiceManager {
public:
    bool register_service(const std::string& name, CmifServiceHandler handler);
    bool dispatch(const std::string& name, const CmifRequest& request,
                  std::vector<uint8_t>& response, HipcError& error) const;
    std::size_t service_count() const noexcept;

private:
    std::map<std::string, CmifServiceHandler> services_;
};

}  // namespace astrans

#endif  // ASTRANS_HIPC_CMIF_H
