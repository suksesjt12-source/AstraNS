#include "nso_loader.h"
#include "mod0_relocator.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace astrans {
namespace {
constexpr std::size_t kHeaderSize = 0x100U;
constexpr uint32_t kTextComp = 1U << 0U;
constexpr uint32_t kRoComp = 1U << 1U;
constexpr uint32_t kDataComp = 1U << 2U;
constexpr uint32_t kTextHash = 1U << 3U;
constexpr uint32_t kRoHash = 1U << 4U;
constexpr uint32_t kDataHash = 1U << 5U;
constexpr uint32_t kExecuteOnly = 1U << 6U;
constexpr uint32_t kZstd = 1U << 7U;
constexpr uint64_t kMaxSegment = 512ULL * 1024ULL * 1024ULL;

uint32_t read_u32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8U) |
           (static_cast<uint32_t>(p[2]) << 16U) |
           (static_cast<uint32_t>(p[3]) << 24U);
}
bool add_u64(uint64_t a, uint64_t b, uint64_t& out) noexcept {
    if (a > std::numeric_limits<uint64_t>::max() - b) return false;
    out = a + b;
    return true;
}
bool align_page(uint64_t value, uint64_t& out) noexcept {
    constexpr uint64_t mask = GuestMemory::kPageSize - 1U;
    if (value > std::numeric_limits<uint64_t>::max() - mask) return false;
    out = (value + mask) & ~mask;
    return true;
}
uint32_t rotr(uint32_t x, unsigned n) noexcept {
    return (x >> n) | (x << (32U - n));
}

std::array<uint8_t, 32> sha256(const uint8_t* data, std::size_t length) noexcept {
    static constexpr uint32_t k[64] = {
        0x428A2F98U,0x71374491U,0xB5C0FBCFU,0xE9B5DBA5U,0x3956C25BU,0x59F111F1U,0x923F82A4U,0xAB1C5ED5U,
        0xD807AA98U,0x12835B01U,0x243185BEU,0x550C7DC3U,0x72BE5D74U,0x80DEB1FEU,0x9BDC06A7U,0xC19BF174U,
        0xE49B69C1U,0xEFBE4786U,0x0FC19DC6U,0x240CA1CCU,0x2DE92C6FU,0x4A7484AAU,0x5CB0A9DCU,0x76F988DAU,
        0x983E5152U,0xA831C66DU,0xB00327C8U,0xBF597FC7U,0xC6E00BF3U,0xD5A79147U,0x06CA6351U,0x14292967U,
        0x27B70A85U,0x2E1B2138U,0x4D2C6DFCU,0x53380D13U,0x650A7354U,0x766A0ABBU,0x81C2C92EU,0x92722C85U,
        0xA2BFE8A1U,0xA81A664BU,0xC24B8B70U,0xC76C51A3U,0xD192E819U,0xD6990624U,0xF40E3585U,0x106AA070U,
        0x19A4C116U,0x1E376C08U,0x2748774CU,0x34B0BCB5U,0x391C0CB3U,0x4ED8AA4AU,0x5B9CCA4FU,0x682E6FF3U,
        0x748F82EEU,0x78A5636FU,0x84C87814U,0x8CC70208U,0x90BEFFFAU,0xA4506CEBU,0xBEF9A3F7U,0xC67178F2U
    };
    std::array<uint32_t, 8> h = {
        0x6A09E667U,0xBB67AE85U,0x3C6EF372U,0xA54FF53AU,
        0x510E527FU,0x9B05688CU,0x1F83D9ABU,0x5BE0CD19U
    };
    auto block = [&](const uint8_t* p) {
        uint32_t w[64]{};
        for (unsigned i=0; i<16U; ++i) {
            const unsigned q=i*4U;
            w[i]=(static_cast<uint32_t>(p[q])<<24U)|
                 (static_cast<uint32_t>(p[q+1U])<<16U)|
                 (static_cast<uint32_t>(p[q+2U])<<8U)|p[q+3U];
        }
        for (unsigned i=16U; i<64U; ++i) {
            const uint32_t s0=rotr(w[i-15U],7U)^rotr(w[i-15U],18U)^(w[i-15U]>>3U);
            const uint32_t s1=rotr(w[i-2U],17U)^rotr(w[i-2U],19U)^(w[i-2U]>>10U);
            w[i]=w[i-16U]+s0+w[i-7U]+s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
        for (unsigned i=0; i<64U; ++i) {
            const uint32_t s1=rotr(e,6U)^rotr(e,11U)^rotr(e,25U);
            const uint32_t ch=(e&f)^((~e)&g);
            const uint32_t t1=z+s1+ch+k[i]+w[i];
            const uint32_t s0=rotr(a,2U)^rotr(a,13U)^rotr(a,22U);
            const uint32_t maj=(a&b)^(a&c)^(b&c);
            const uint32_t t2=s0+maj;
            z=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=z;
    };
    std::size_t offset=0U;
    while (length-offset>=64U) { block(data+offset); offset+=64U; }
    std::array<uint8_t,128> tail{};
    const std::size_t remain=length-offset;
    if (remain) std::memcpy(tail.data(),data+offset,remain);
    tail[remain]=0x80U;
    const std::size_t tail_size=remain<56U?64U:128U;
    const uint64_t bits=static_cast<uint64_t>(length)*8U;
    for (unsigned i=0;i<8U;++i) tail[tail_size-1U-i]=static_cast<uint8_t>(bits>>(i*8U));
    block(tail.data());
    if (tail_size==128U) block(tail.data()+64U);
    std::array<uint8_t,32> digest{};
    for (unsigned i=0;i<8U;++i) {
        digest[i*4U]=static_cast<uint8_t>(h[i]>>24U);
        digest[i*4U+1U]=static_cast<uint8_t>(h[i]>>16U);
        digest[i*4U+2U]=static_cast<uint8_t>(h[i]>>8U);
        digest[i*4U+3U]=static_cast<uint8_t>(h[i]);
    }
    return digest;
}

bool lz4_block(const uint8_t* src, std::size_t src_size,
               uint8_t* dst, std::size_t dst_size) noexcept {
    std::size_t in=0U,out=0U;
    while (in<src_size) {
        const uint8_t token=src[in++];
        std::size_t literals=token>>4U;
        if (literals==15U) {
            uint8_t ext=255U;
            while (ext==255U) {
                if (in>=src_size) return false;
                ext=src[in++];
                if (out>dst_size || ext>dst_size-out || literals>dst_size-out-ext) return false;
                literals+=ext;
            }
        }
        if (literals>src_size-in || literals>dst_size-out) return false;
        if (literals) std::memcpy(dst+out,src+in,literals);
        in+=literals; out+=literals;
        if (in==src_size) return out==dst_size;
        if (src_size-in<2U) return false;
        const std::size_t distance=static_cast<std::size_t>(src[in])|
                                   (static_cast<std::size_t>(src[in+1U])<<8U);
        in+=2U;
        if (distance==0U || distance>out) return false;
        std::size_t match=(token&0x0FU)+4U;
        if ((token&0x0FU)==15U) {
            uint8_t ext=255U;
            while (ext==255U) {
                if (in>=src_size) return false;
                ext=src[in++];
                if (out>dst_size || ext>dst_size-out || match>dst_size-out-ext) return false;
                match+=ext;
            }
        }
        if (match>dst_size-out) return false;
        for (std::size_t i=0;i<match;++i) dst[out+i]=dst[out+i-distance];
        out+=match;
    }
    return out==dst_size;
}

struct Segment {
    uint32_t file_offset=0;
    uint32_t memory_offset=0;
    uint32_t file_size=0;
    uint32_t memory_size=0;
    uint64_t mapped_size=0;
    uint32_t compression_flag=0;
    uint32_t hash_flag=0;
    uint32_t hash_offset=0;
    MemoryPermission final_permissions=MemoryPermission::Read;
    std::vector<uint8_t> unpacked;
};
}  // namespace

bool load_nso(GuestMemory& memory, const uint8_t* file_data, std::size_t file_size,
              uint64_t guest_base, LoadedNso& loaded, NsoLoadError& error) {
    loaded={}; error=NsoLoadError::None;
    if (!file_data) { error=NsoLoadError::NullInput; return false; }
    if (file_size<kHeaderSize) { error=NsoLoadError::TooSmall; return false; }
    if (file_data[0]!='N'||file_data[1]!='S'||file_data[2]!='O'||file_data[3]!='0') {
        error=NsoLoadError::BadMagic; return false;
    }
    if ((guest_base%GuestMemory::kPageSize)!=0U) { error=NsoLoadError::InvalidSegment; return false; }

    const uint32_t flags=read_u32(file_data+0x0CU);
    if (flags&~0xFFU) { error=NsoLoadError::InvalidFlags; return false; }
    if ((flags&kZstd)!=0U && (flags&(kTextComp|kRoComp|kDataComp))!=0U) {
        error=NsoLoadError::UnsupportedCompression; return false;
    }

    const uint32_t text_size=read_u32(file_data+0x18U);
    const uint32_t ro_size=read_u32(file_data+0x28U);
    const uint32_t data_size=read_u32(file_data+0x38U);
    const uint32_t bss_size=read_u32(file_data+0x3CU);
    if (text_size==0U || read_u32(file_data+0x14U)!=0U) {
        error=NsoLoadError::InvalidSegment; return false;
    }

    std::array<Segment,3> segs{{
        {read_u32(file_data+0x10U),read_u32(file_data+0x14U),read_u32(file_data+0x60U),
         text_size,0U,kTextComp,kTextHash,0xA0U,
         (flags&kExecuteOnly)?MemoryPermission::Execute:
             (MemoryPermission::Read|MemoryPermission::Execute),{}},
        {read_u32(file_data+0x20U),read_u32(file_data+0x24U),read_u32(file_data+0x64U),
         ro_size,0U,kRoComp,kRoHash,0xC0U,MemoryPermission::Read,{}},
        {read_u32(file_data+0x30U),read_u32(file_data+0x34U),read_u32(file_data+0x68U),
         data_size,0U,kDataComp,kDataHash,0xE0U,
         MemoryPermission::Read|MemoryPermission::Write,{}}
    }};

    uint64_t total=0U;
    uint64_t image_end=0U;
    for (Segment& s:segs) {
        if (s.memory_size==0U) {
            if (s.file_size!=0U) { error=NsoLoadError::InvalidSegment; return false; }
            continue;
        }
        if (s.memory_size>kMaxSegment || s.file_offset<kHeaderSize ||
            s.file_offset>file_size || s.file_size==0U ||
            s.file_size>file_size-s.file_offset ||
            s.memory_offset%GuestMemory::kPageSize!=0U) {
            error=NsoLoadError::InvalidSegment; return false;
        }
        uint64_t new_total=0U;
        if (!add_u64(total,s.memory_size,new_total)||new_total>kMaxSegment) {
            error=NsoLoadError::InvalidSegment; return false;
        }
        total=new_total;
        if (!align_page(s.memory_size,s.mapped_size)) {
            error=NsoLoadError::AddressOverflow; return false;
        }
        uint64_t guest_addr=0U,guest_end=0U,relative_end=0U;
        if (!add_u64(guest_base,s.memory_offset,guest_addr)||
            !add_u64(guest_addr,s.mapped_size,guest_end)||
            !add_u64(s.memory_offset,s.mapped_size,relative_end)) {
            error=NsoLoadError::AddressOverflow; return false;
        }
        image_end=std::max(image_end,static_cast<uint64_t>(s.memory_offset)+s.memory_size);
        s.unpacked.resize(s.memory_size);
        if (flags&s.compression_flag) {
            if (!lz4_block(file_data+s.file_offset,s.file_size,
                           s.unpacked.data(),s.memory_size)) {
                error=NsoLoadError::DecompressionFailed; return false;
            }
        } else {
            if (s.file_size!=s.memory_size) { error=NsoLoadError::InvalidSegment; return false; }
            std::memcpy(s.unpacked.data(),file_data+s.file_offset,s.memory_size);
        }
        if (flags&s.hash_flag) {
            const auto hash=sha256(s.unpacked.data(),s.unpacked.size());
            if (!std::equal(hash.begin(),hash.end(),file_data+s.hash_offset)) {
                error=NsoLoadError::HashMismatch; return false;
            }
        }
    }

    uint64_t data_bss=0U;
    if (!add_u64(data_size,bss_size,data_bss)||data_bss>kMaxSegment) {
        error=NsoLoadError::InvalidSegment; return false;
    }
    std::array<uint64_t,3> map_sizes{};
    for (std::size_t i=0;i<3U;++i) map_sizes[i]=segs[i].mapped_size;
    if (data_bss!=0U) {
        if (segs[2].memory_offset%GuestMemory::kPageSize!=0U||
            !align_page(data_bss,map_sizes[2])) {
            error=NsoLoadError::InvalidSegment; return false;
        }
        uint64_t data_end=0U,guest_addr=0U,guest_end=0U;
        if (!add_u64(segs[2].memory_offset,data_bss,data_end)||
            !add_u64(guest_base,segs[2].memory_offset,guest_addr)||
            !add_u64(guest_addr,map_sizes[2],guest_end)) {
            error=NsoLoadError::AddressOverflow; return false;
        }
        image_end=std::max(image_end,data_end);
    }

    for (std::size_t i=0;i<3U;++i) {
        if (map_sizes[i]==0U) continue;
        const uint64_t start_i=segs[i].memory_offset;
        const uint64_t end_i=start_i+map_sizes[i];
        for (std::size_t j=i+1U;j<3U;++j) {
            if (map_sizes[j]==0U) continue;
            const uint64_t start_j=segs[j].memory_offset;
            const uint64_t end_j=start_j+map_sizes[j];
            if (start_i<end_j&&start_j<end_i) {
                error=NsoLoadError::InvalidSegment; return false;
            }
        }
    }

    const auto temporary=MemoryPermission::Read|MemoryPermission::Write|MemoryPermission::Execute;
    std::array<uint64_t,3> mapped{};
    std::size_t count=0U;
    auto rollback=[&]() {
        while(count) { --count; memory.unmap(mapped[count]); }
    };
    for (std::size_t i=0;i<3U;++i) {
        if (map_sizes[i]==0U) continue;
        const uint64_t address=guest_base+segs[i].memory_offset;
        if (!memory.map(address,static_cast<std::size_t>(map_sizes[i]),temporary)) {
            rollback(); error=NsoLoadError::MappingFailed; return false;
        }
        mapped[count++]=address;
    }
    for (const Segment& s:segs) {
        if (!s.memory_size) continue;
        if (!memory.write(guest_base+s.memory_offset,s.unpacked.data(),s.unpacked.size())) {
            rollback(); error=NsoLoadError::CopyFailed; return false;
        }
    }
    for (std::size_t i=0;i<3U;++i) {
        if (map_sizes[i]==0U) continue;
        if (!memory.protect(guest_base+segs[i].memory_offset,
                            static_cast<std::size_t>(map_sizes[i]),segs[i].final_permissions)) {
            rollback(); error=NsoLoadError::MappingFailed; return false;
        }
    }

    Mod0Error mod0_error=Mod0Error::None;
    if (!apply_mod0_relative_relocations(memory,guest_base,image_end,text_size,mod0_error) ||
        (mod0_error!=Mod0Error::None && mod0_error!=Mod0Error::NoModuleHeader)) {
        rollback();
        error=NsoLoadError::RelocationFailed;
        return false;
    }

    loaded.base_address=guest_base;
    loaded.entry_point=guest_base+segs[0].memory_offset;
    loaded.bss_start=guest_base+segs[2].memory_offset+data_size;
    loaded.image_size=image_end;
    loaded.text_size=text_size;
    loaded.ro_size=ro_size;
    loaded.data_size=data_size;
    loaded.bss_size=bss_size;
    return true;
}
}  // namespace astrans
