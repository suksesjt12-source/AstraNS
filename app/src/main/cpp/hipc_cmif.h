#ifndef ASTRANS_HIPC_CMIF_H
#define ASTRANS_HIPC_CMIF_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
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


struct CmifDomainRequest {
    uint8_t request_kind = 0;
    uint32_t target_object_id = 0;
    uint32_t token = 0;
    CmifRequest command;
    std::vector<uint32_t> input_object_ids;
};

bool parse_cmif_domain_request(const uint8_t* data, std::size_t size,
                               const HipcMessage& message,
                               CmifDomainRequest& request, HipcError& error);
bool build_cmif_domain_response(uint32_t output_object_count,
                               const uint8_t* cmif_payload,
                               std::size_t cmif_payload_size,
                               std::vector<uint8_t>& output,
                               HipcError& error);

enum class IpcObjectType : uint8_t { Session, Event, Port, SharedMemory, Other };

struct IpcKernelObject {
    explicit IpcKernelObject(IpcObjectType object_type) : type(object_type) {}
    virtual ~IpcKernelObject() = default;
    IpcObjectType type;
};

class IpcHandleTable {
public:
    uint32_t insert(std::shared_ptr<IpcKernelObject> object);
    uint32_t duplicate(uint32_t handle);
    uint32_t move(uint32_t handle);
    bool close(uint32_t handle);
    std::shared_ptr<IpcKernelObject> get(uint32_t handle) const;
    std::size_t size() const noexcept;
private:
    uint32_t next_handle_ = 1U;
    std::map<uint32_t, std::shared_ptr<IpcKernelObject>> handles_;
};

class CmifDomainTable {
public:
    uint32_t insert(CmifServiceHandler handler);
    bool remove(uint32_t object_id);
    const CmifServiceHandler* get(uint32_t object_id) const noexcept;
    std::size_t size() const noexcept;
private:
    uint32_t next_object_id_ = 1U;
    std::map<uint32_t, CmifServiceHandler> objects_;
};

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
