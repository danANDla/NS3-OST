#ifndef OST_HEADER_H
#define OST_HEADER_H

#include <inttypes.h>
#include <stdbool.h>

#include "ns3/header.h"
#include "spw_packet.h"

namespace ns3 {

typedef enum {
    ACK,
    SYN,
    RST,
    DTA // virtual
} SegmentFlag;

class OstHeader: public Header {
    typedef uint8_t FlagOctet;

    public:
        static TypeId GetTypeId();
        TypeId GetInstanceTypeId() const override;
        void Print(std::ostream& os) const override;
        uint32_t GetSerializedSize() const override;
        void Serialize(Buffer::Iterator start) const override;
        uint32_t Deserialize(Buffer::Iterator start) override;

        void set_seq_number(uint8_t);
        uint8_t get_seq_number();

        void set_src_addr(uint8_t);
        uint8_t get_src_addr();

        void set_payload_len(uint8_t);
        uint8_t get_payload_len();

        void set_flag(SegmentFlag flag);
        void unset_flat(SegmentFlag flag);

        bool is_ack();
        bool is_syn();
        bool is_rst();
        bool is_dta();
    private:
        FlagOctet flags;
        uint8_t seq_number;
        uint8_t source_addr;
        uint8_t payload_length;
};

}
#endif