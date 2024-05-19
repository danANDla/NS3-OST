#ifndef OST_NODE_H
#define OST_NODE_H

#include "timer_fifo.h"

#include "ns3/callback.h"
#include "ns3/object.h"
#include "ns3/ost-header.h"
#include "ns3/ptr.h"
#include "ns3/spw-device.h"

#include <inttypes.h>

/**
 * \defgroup ost Open SpaceWire Transport Layer Node
 * This section documents the API of the transport-layer node of spw network.
 */
namespace ns3
{

class OstSocket;

typedef enum
{
    PACKET_ARRIVED_FROM_NETWORK = 0,
    APPLICATION_PACKET_READY,
    RETRANSMISSION_INTERRUPT,
    TRANSPORT_CLK_INTERRUPT,
} TransportLayerEvent;

class OstNode : public Object
{
    static const uint16_t WINDOW_SZ = 10;
    static const micros_t DURATION_RETRANSMISSON = 10000;
    static const uint8_t PORTS_NUMBER = 3;

  public:
    OstNode(Ptr<SpWDevice>, int8_t mode);
    ~OstNode();

    int8_t event_handler(const TransportLayerEvent e);
    int8_t add_packet_to_tx(Ptr<Packet> p);

    void start();
    void shutdown();

    int8_t open_connection(uint8_t address);
    int8_t close_connection(uint8_t address);
    int8_t send_packet(uint8_t address, const uint8_t* buffer, uint32_t size);
    int8_t receive_packet(const uint8_t* buffer, uint32_t& received_sz);

    int8_t get_socket(uint8_t address, Ptr<OstSocket>& socket);

    typedef Callback<void, uint8_t, Ptr<Packet>> ReceiveCallback;
    void SetReceiveCallback(OstNode::ReceiveCallback cb);
    Ptr<SpWDevice> GetSpWLayer();

  private:
    void send_to_application(Ptr<Packet> packet);
    int8_t get_packet_from_application();
    void add_packet_to_rx(Ptr<Packet>);

    int8_t send_to_physical(SegmentFlag t, uint8_t seq_n);
    int8_t get_packet_from_physical();
    bool tx_sliding_window_have_space();
    bool in_tx_window(uint8_t);
    bool in_rx_window(uint8_t);
    bool hw_timer_handler(uint8_t);
    bool network_layer_handler(Ptr<NetDevice> dev,
                               Ptr<const Packet> pkt,
                               uint16_t mode,
                               const Address& sender);
    int8_t aggregate_socket(uint8_t address);
    int8_t delete_socket(uint8_t address);

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

    uint8_t self_address;

    std::string segment_type_name(SegmentFlag t);
    std::string event_name(TransportLayerEvent e);
    ReceiveCallback rx_cb;

    std::vector<Ptr<OstSocket>> ports;
};
} // namespace ns3

#endif