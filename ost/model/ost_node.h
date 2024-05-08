#ifndef OST_NODE_H
#define OST_NODE_H

#include <inttypes.h>

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/spw-device.h"
#include "ns3/ost-header.h"
#include "ns3/callback.h"

#include "timer_fifo.h"

/**
 * \defgroup ost Open SpaceWire Transport Layer Node
 * This section documents the API of the transport-layer node of spw network.
 */
namespace ns3 {
typedef enum {
    PACKET_ARRIVED_FROM_NETWORK = 0,
    APPLICATION_PACKET_READY,
    RETRANSMISSION_INTERRUPT,
    TRANSPORT_CLK_INTERRUPT,
} TransportLayerEvent;

class OstNode: public Object {
    static const uint16_t WINDOW_SZ = 1;
    public:
        OstNode(uint8_t id, Ptr<SpWDevice>);
        ~OstNode();
        int8_t event_handler(const TransportLayerEvent e);

        void add_packet_to_rx(Ptr<Packet> p);
        int8_t add_packet_to_tx(Ptr<Packet> p);

        typedef Callback<void, uint8_t, Ptr<Packet>> ReceiveCallback;
        void SetReceiveCallback(OstNode::ReceiveCallback cb);
    private:
        void send_to_application(Ptr<Packet> packet);
        int8_t get_packet_from_application();

        int8_t send_to_physical(SegmentType t, uint8_t seq_n);
        int8_t get_packet_from_physical();
        bool tx_sliding_window_have_space();
        bool in_tx_window(uint8_t);
        bool in_rx_window(uint8_t);
        bool hw_timer_handler(uint8_t);
        bool network_layer_handler(Ptr<NetDevice> dev, Ptr<const Packet> pkt, uint16_t mode, const Address& sender);

        uint8_t to_retr;
        uint8_t tx_window_bottom;
        uint8_t tx_window_top;
        uint8_t rx_window_bottom;
        uint8_t rx_window_top;
        std::vector<Ptr<Packet>> tx_buffer;
        std::vector<Ptr<Packet>> rx_buffer;
        std::vector<bool> acknowledged;
        TimerFifo queue;
        Ptr<SpWDevice> spw_layer;

        uint8_t simulator_id;
        std::string segment_type_name(SegmentType t);
        std::string event_name(TransportLayerEvent e);
        ReceiveCallback rx_cb;
};
}

#endif