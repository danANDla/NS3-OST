#include "ost-header.h"

#include <string.h>

namespace ns3 
{
    TypeId
    OstHeader::GetTypeId()
    {
        static TypeId tid = TypeId("ns3::OstHeader")
                                .SetParent<Header>()
                                .SetGroupName("Spw")
                                .AddConstructor<OstHeader>();
        return tid;
    }

    TypeId
    OstHeader::GetInstanceTypeId() const
    {
        return GetTypeId();
    }

    void
    OstHeader::Print(std::ostream& os) const
    {
        os << "flags: " << std::to_string(flags) <<  ", seq_n: " << std::to_string(seq_number)<< ", payload_length: " << std::to_string(payload_length)  << ", src arddr: " << std::to_string(source_addr);
    }

    uint32_t
    OstHeader::GetSerializedSize() const
    {
        return 4;
    }

    void
    OstHeader::Serialize(Buffer::Iterator start) const
    {
        Buffer::Iterator i = start;

        i.WriteU8(flags);
        i.WriteU8(source_addr);
        i.WriteU8(seq_number);
        i.WriteU8(payload_length);
    }

    uint32_t
    OstHeader::Deserialize(Buffer::Iterator start)
    {
        Buffer::Iterator i = start;
        flags = i.ReadU8();
        source_addr = i.ReadU8();
        seq_number = i.ReadU8();
        payload_length = i.ReadU8();
        return GetSerializedSize();
    }

    void OstHeader::set_seq_number(uint8_t n) {
        seq_number = n;
    }
    uint8_t OstHeader::get_seq_number() {
        return seq_number;
    }

    void OstHeader::set_src_addr(uint8_t addr) {
        source_addr = addr;
    }
    uint8_t OstHeader::get_src_addr() {
        return source_addr;
    }

    void OstHeader::set_payload_len(uint8_t l) {
        payload_length = l;
    }
    uint8_t OstHeader::get_payload_len() {
        return payload_length;
    }

    void OstHeader::set_flag(SegmentFlag flag) {
        switch (flag)
        {
        case ACK:
            flags |= 0b00000001;
            break;
        case SYN:
            flags |= 0b00000010;
            break;
        case RST:
            flags |= 0b00000100;
            break;
        default:
            flags &= 0b11111000;
            break;
        }
    }

    bool OstHeader::is_ack() {
        return flags & 0b00000001;
    }
    bool OstHeader::is_syn() {
        return flags & 0b00000010;
    }
    bool OstHeader::is_rst() {
        return flags & 0b00000100;
    }
    bool OstHeader::is_dta() {
        return (flags & 0b00000111) == 0;
    }
}
