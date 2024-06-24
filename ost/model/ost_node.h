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
        SPW_READY,
    } TransportLayerEvent;

    class OstNode : public Object
    {
        static const uint8_t PORTS_NUMBER = 3;
        static const micros_t DURATION_RETRANSMISSON = 300000;

    public:
        int8_t start(uint8_t hw_timer_id);
        void shutdown();
        int8_t open_connection(uint8_t address);
        int8_t close_connection(uint8_t address);
        int8_t event_handler(const TransportLayerEvent e);
        int8_t send_packet(uint8_t address, const uint8_t *buffer, uint32_t size);

        /*
        *  NS-3 Specific
        */
        OstNode(Ptr<SpWDevice>, int8_t mode);
        OstNode(Ptr<SpWDevice>, int8_t mode, uint16_t window_sz);
        ~OstNode();

        int8_t GetSocket(uint8_t address, Ptr<OstSocket> &socket);
        int8_t AggregateSocket(uint8_t address);
        int8_t DeleteSocket(uint8_t address);
        Ptr<SpWDevice> GetSpWLayer();
        uint8_t GetAddress() const;
        typedef Callback<void, uint8_t, Ptr<Packet>> ReceiveCallback;
        void SetReceiveCallback(OstNode::ReceiveCallback cb);

    private:

        uint8_t self_address;
        std::vector<OstSocket*> ports;
        Ptr<SpWDevice> spw_layer;
        Ptr<Packet> that_arrived;

        /*
        *  NS-3 Specific
        */
        uint16_t WINDOW_SZ;
        ReceiveCallback rx_cb;
        bool NetworkLayerReceive(Ptr<NetDevice> dev,
                                   Ptr<const Packet> pkt,
                                   uint16_t mode,
                                   const Address &sender);
        std::string GetSegmentTypeName(SegmentFlag t);
        std::string GetTransportEventName(TransportLayerEvent e);
        void SpwReadyHandler();
    };
} // namespace ns3

#endif