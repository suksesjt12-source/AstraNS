#include "mod0_relocator.h"

#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace astrans {
namespace {
constexpr uint64_t kDtNull=0U;
constexpr uint64_t kDtRela=7U;
constexpr uint64_t kDtRelaSz=8U;
constexpr uint64_t kDtRelaEnt=9U;
constexpr uint64_t kDtRelrSz=35U;
constexpr uint64_t kDtRelr=36U;
constexpr uint64_t kDtRelrEnt=37U;
constexpr uint32_t kRaarch64None=0U;
constexpr uint32_t kRaarch64Relative=1027U;
constexpr std::size_t kMod0HeaderSize=0x18U;
constexpr std::size_t kDynamicEntrySize=16U;
constexpr uint64_t kMaxDynamicEntries=4096U;
constexpr uint64_t kMaxRelocationEntries=1U<<20U;

uint32_t read_u32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0])|
           (static_cast<uint32_t>(p[1])<<8U)|
           (static_cast<uint32_t>(p[2])<<16U)|
           (static_cast<uint32_t>(p[3])<<24U);
}
uint64_t read_u64(const uint8_t* p) noexcept {
    uint64_t value=0U;
    for(unsigned i=0;i<8U;++i) value|=static_cast<uint64_t>(p[i])<<(i*8U);
    return value;
}
int64_t read_i32(const uint8_t* p) noexcept {
    return static_cast<int32_t>(read_u32(p));
}
bool add_signed(uint64_t base,int64_t offset,uint64_t& out) noexcept {
    if(offset>=0) {
        const uint64_t amount=static_cast<uint64_t>(offset);
        if(base>UINT64_MAX-amount) return false;
        out=base+amount;
    } else {
        const uint64_t amount=static_cast<uint64_t>(-(offset+1))+1U;
        if(base<amount) return false;
        out=base-amount;
    }
    return true;
}
bool add_unsigned(uint64_t a,uint64_t b,uint64_t& out) noexcept {
    if(a>UINT64_MAX-b) return false;
    out=a+b;
    return true;
}
bool image_range(uint64_t image_base,uint64_t image_size,uint64_t address,
                 uint64_t length) noexcept {
    if(address<image_base) return false;
    const uint64_t offset=address-image_base;
    return offset<=image_size && length<=image_size-offset;
}
bool clear_bss(GuestMemory& memory,uint64_t start,uint64_t end) {
    if(end<start) return false;
    std::array<uint8_t,4096> zeros{};
    while(start<end) {
        const uint64_t remaining=end-start;
        const std::size_t chunk=static_cast<std::size_t>(
            remaining<zeros.size()?remaining:zeros.size());
        if(!memory.write(start,zeros.data(),chunk)) return false;
        start+=chunk;
    }
    return true;
}
}  // namespace

bool apply_mod0_relative_relocations(GuestMemory& memory,uint64_t image_base,
                                     uint64_t image_size,uint64_t text_size,
                                     Mod0Error& error) {
    error=Mod0Error::None;
    if(text_size<8U || !image_range(image_base,image_size,image_base,8U)) {
        error=Mod0Error::InvalidHeader;
        return false;
    }
    std::array<uint8_t,4> offset_bytes{};
    if(!memory.read(image_base+4U,offset_bytes.data(),offset_bytes.size())) {
        error=Mod0Error::MemoryFault;
        return false;
    }
    const int64_t mod0_delta=read_i32(offset_bytes.data());
    uint64_t mod0_address=0U;
    if(!add_signed(image_base,mod0_delta,mod0_address)) {
        error=Mod0Error::AddressOverflow;
        return false;
    }
    if(!image_range(image_base,image_size,mod0_address,kMod0HeaderSize)) {
        error=Mod0Error::InvalidHeader;
        return false;
    }
    std::array<uint8_t,kMod0HeaderSize> header{};
    if(!memory.read(mod0_address,header.data(),header.size())) {
        error=Mod0Error::MemoryFault;
        return false;
    }
    if(header[0]!='M'||header[1]!='O'||header[2]!='D'||header[3]!='0') {
        error=Mod0Error::NoModuleHeader;
        return true;
    }

    uint64_t dynamic_address=0U,bss_start=0U,bss_end=0U;
    if(!add_signed(mod0_address,read_i32(header.data()+4U),dynamic_address)||
       !add_signed(mod0_address,read_i32(header.data()+8U),bss_start)||
       !add_signed(mod0_address,read_i32(header.data()+12U),bss_end)) {
        error=Mod0Error::AddressOverflow;
        return false;
    }
    if(!image_range(image_base,image_size,dynamic_address,kDynamicEntrySize)||
       !image_range(image_base,image_size,bss_start,0U)||
       !image_range(image_base,image_size,bss_end,0U)||
       bss_end<bss_start) {
        error=Mod0Error::InvalidHeader;
        return false;
    }
    if(bss_end>bss_start&&!clear_bss(memory,bss_start,bss_end)) {
        error=Mod0Error::MemoryFault;
        return false;
    }

    uint64_t rela_pointer=0U,rela_size=0U,rela_entry_size=0U;
    uint64_t relr_pointer=0U,relr_size=0U,relr_entry_size=0U;
    bool has_rela=false,has_rela_size=false,has_rela_entry=false;
    bool has_relr=false,has_relr_size=false,has_relr_entry=false;
    bool terminated=false;
    for(uint64_t i=0;i<kMaxDynamicEntries;++i) {
        uint64_t entry_address=0U;
        if(!add_unsigned(dynamic_address,i*kDynamicEntrySize,entry_address)||
           !image_range(image_base,image_size,entry_address,kDynamicEntrySize)) {
            error=Mod0Error::InvalidDynamicSection;
            return false;
        }
        std::array<uint8_t,kDynamicEntrySize> entry{};
        if(!memory.read(entry_address,entry.data(),entry.size())) {
            error=Mod0Error::MemoryFault;
            return false;
        }
        const uint64_t tag=read_u64(entry.data());
        const uint64_t value=read_u64(entry.data()+8U);
        if(tag==kDtNull) { terminated=true; break; }
        switch(tag) {
            case kDtRela: rela_pointer=value; has_rela=true; break;
            case kDtRelaSz: rela_size=value; has_rela_size=true; break;
            case kDtRelaEnt: rela_entry_size=value; has_rela_entry=true; break;
            case kDtRelr: relr_pointer=value; has_relr=true; break;
            case kDtRelrSz: relr_size=value; has_relr_size=true; break;
            case kDtRelrEnt: relr_entry_size=value; has_relr_entry=true; break;
            default: break;
        }
    }
    if(!terminated) {
        error=Mod0Error::InvalidDynamicSection;
        return false;
    }

    if(has_rela||has_rela_size||has_rela_entry) {
        if(!has_rela||!has_rela_size||!has_rela_entry||rela_entry_size!=24U||
           (rela_size%rela_entry_size)!=0U||
           rela_size/rela_entry_size>kMaxRelocationEntries) {
            error=Mod0Error::InvalidRelocationTable;
            return false;
        }
        uint64_t table_address=0U;
        if(!add_unsigned(image_base,rela_pointer,table_address)||
           !image_range(image_base,image_size,table_address,rela_size)) {
            error=Mod0Error::InvalidRelocationTable;
            return false;
        }
        for(uint64_t offset=0U;offset<rela_size;offset+=24U) {
            std::array<uint8_t,24> rela{};
            if(!memory.read(table_address+offset,rela.data(),rela.size())) {
                error=Mod0Error::MemoryFault; return false;
            }
            const uint64_t target_offset=read_u64(rela.data());
            const uint64_t info=read_u64(rela.data()+8U);
            const uint32_t type=static_cast<uint32_t>(info);
            const uint32_t symbol=static_cast<uint32_t>(info>>32U);
            if(type==kRaarch64None) continue;
            if(type!=kRaarch64Relative||symbol!=0U) {
                error=Mod0Error::UnsupportedRelocation;
                return false;
            }
            uint64_t target=0U,value=0U,relocated=0U;
            if(!add_unsigned(image_base,target_offset,target)||
               !image_range(image_base,image_size,target,8U)) {
                error=Mod0Error::InvalidRelocationTable; return false;
            }
            const int64_t addend=static_cast<int64_t>(read_u64(rela.data()+16U));
            if(!add_signed(image_base,addend,relocated)||
               !memory.write64(target,relocated)) {
                error=Mod0Error::MemoryFault; return false;
            }
        }
    }

    if(has_relr||has_relr_size||has_relr_entry) {
        if(!has_relr||!has_relr_size||!has_relr_entry||relr_entry_size!=8U||
           (relr_size%8U)!=0U||relr_size/8U>kMaxRelocationEntries) {
            error=Mod0Error::InvalidRelocationTable;
            return false;
        }
        uint64_t table_address=0U;
        if(!add_unsigned(image_base,relr_pointer,table_address)||
           !image_range(image_base,image_size,table_address,relr_size)) {
            error=Mod0Error::InvalidRelocationTable;
            return false;
        }
        uint64_t next_offset=0U;
        for(uint64_t offset=0U;offset<relr_size;offset+=8U) {
            std::array<uint8_t,8> item{};
            if(!memory.read(table_address+offset,item.data(),item.size())) {
                error=Mod0Error::MemoryFault; return false;
            }
            const uint64_t entry=read_u64(item.data());
            if((entry&1U)==0U) {
                uint64_t target=0U,value=0U,relocated=0U;
                if(!add_unsigned(image_base,entry,target)||
                   !image_range(image_base,image_size,target,8U)||
                   !memory.read64(target,value)||!add_unsigned(image_base,value,relocated)||
                   !memory.write64(target,relocated)||!add_unsigned(entry,8U,next_offset)) {
                    error=Mod0Error::MemoryFault; return false;
                }
            } else {
                const uint64_t bitmap=entry>>1U;
                for(unsigned bit=0U;bit<63U;++bit) {
                    if((bitmap&(uint64_t{1}<<bit))==0U) continue;
                    uint64_t target_offset=0U,target=0U,value=0U,relocated=0U;
                    const uint64_t delta=(static_cast<uint64_t>(bit)+1U)*8U;
                    if(!add_unsigned(next_offset,delta,target_offset)||
                       !add_unsigned(image_base,target_offset,target)||
                       !image_range(image_base,image_size,target,8U)||
                       !memory.read64(target,value)||!add_unsigned(image_base,value,relocated)||
                       !memory.write64(target,relocated)) {
                        error=Mod0Error::MemoryFault; return false;
                    }
                }
                if(!add_unsigned(next_offset,63U*8U,next_offset)) {
                    error=Mod0Error::AddressOverflow; return false;
                }
            }
        }
    }
    return true;
}
}  // namespace astrans
