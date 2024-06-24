#include "ost_node.h"
#include "ost_socket.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/spw-channel.h"
#include "ns3/drop-tail-queue.h"

#include <stdio.h>
#include <string.h>

namespace ns3
{

    NS_LOG_COMPONENT_DEFINE("OstNode");

    OstNode::OstNode(Ptr<SpWDevice> dev, int8_t mode)
        : spw_layer(dev),
          ports(std::vector<OstSocket*>()),
          WINDOW_SZ(1)
    {
        spw_layer->GetAddress().CopyTo(&self_address);
        spw_layer->SetReceiveCallback(MakeCallback(&OstNode::NetworkLayerReceive, this));
        spw_layer->SetDeviceReadyCallback(MakeCallback(&OstNode::SpwReadyHandler, this));
    }

    OstNode::OstNode(Ptr<SpWDevice> dev, int8_t mode, uint16_t window_sz)
        : spw_layer(dev),
          ports(std::vector<OstSocket*>()),
          WINDOW_SZ(window_sz)
    {
        spw_layer->GetAddress().CopyTo(&self_address);
        spw_layer->SetReceiveCallback(MakeCallback(&OstNode::NetworkLayerReceive, this));
        spw_layer->SetDeviceReadyCallback(MakeCallback(&OstNode::SpwReadyHandler, this));
    }

    OstNode::~OstNode()
    {
    }

    int8_t
    OstNode::start(uint8_t hw_timer_id)
    {
        OstSocket* smth = new OstSocket(this);
        ports.push_back(smth);
        spw_layer->ErrorResetSpWState();
        open_connection(1 - hw_timer_id);
        return 0;
    }

    void
    OstNode::shutdown()
    {
        for (int i = 0; i < ports.size(); ++i)
        {
            if (ports[i] && ports[i]->GetAddress() != self_address && ports[i]->GetState() != OstSocket::State::CLOSED)
            {
                ports[i]->close();
            }
        }
        spw_layer->Shutdown();
    }

    int8_t
    OstNode::open_connection(uint8_t addr)
    {
        if (addr == self_address)
        {
            NS_LOG_ERROR("bad address\n");
            return -1;
        }

        Ptr<OstSocket> sk;
        int8_t r = GetSocket(addr, sk);
        if (r != 1) // create new
        {
            int8_t r = AggregateSocket(addr);
            if (r != -1)
                ports[r]->open(OstSocket::Mode::CONNECTIONLESS);
        }
        else
        {
            if (sk->GetState() == OstSocket::OPEN)
                return 0; // already opened
            sk->open(OstSocket::Mode::CONNECTIONLESS);
        }
        return 1;
    }

    int8_t
    OstNode::close_connection(uint8_t addr)
    {
        if (addr == self_address)
            return -1;

        for (int i = 0; i < ports.size(); ++i)
        {
            if (ports[i]->GetAddress() == addr)
            {
                ports[i]->close();
                return 1;
            }
        }
        return 0;
    }

    int8_t
    OstNode::event_handler(const TransportLayerEvent e)
    {
        switch (e)
        {
        case PACKET_ARRIVED_FROM_NETWORK:
            if (that_arrived)
            {
                ports[0]->socket_event_handler(OstSocket::Event::PACKET_ARRIVED_FROM_NETWORK, that_arrived, 0);
            }
            break;
        case APPLICATION_PACKET_READY:
            break;
        default:
            break;
        }
        return 0;
    }

    int8_t
    OstNode::GetSocket(uint8_t addr, Ptr<OstSocket> &sk)
    {
        for (int i = 0; i < ports.size(); ++i)
        {
            if (ports[i] && ports[i]->GetAddress() == addr)
            {
                sk = ports[i];
                return 1;
            }
        }
        return -1;
    }

    int8_t
    OstNode::AggregateSocket(uint8_t address)
    {
        for (int i = 0; i < ports.size(); ++i)
        {
            if (!ports[i]->IsAggregated())
            {
                ports[i]->SetAddress(address);
                ports[i]->SetAggregated(true);
                return i;
            }
        }
        return -1;
    }

    int8_t
    OstNode::DeleteSocket(uint8_t address)
    {
        for (int i = 0; i < ports.size(); ++i)
        {
            if (ports[i]->GetAddress() == address)
            {
                return 1;
            }
        }
        return -1;
    }

    Ptr<SpWDevice>
    OstNode::GetSpWLayer()
    {
        return spw_layer;
    }

    uint8_t
    OstNode::GetAddress() const { 
        return self_address;
    }

    void
    OstNode::SetReceiveCallback(ReceiveCallback cb)
    {
        rx_cb = cb;
    }

    bool
    OstNode::NetworkLayerReceive(Ptr<NetDevice> dev,
                                 Ptr<const Packet> pkt,
                                 uint16_t mode,
                                 const Address &sender)
    {
        Simulator::ScheduleNow(&OstSocket::socket_event_handler, ports[0], OstSocket::Event::PACKET_ARRIVED_FROM_NETWORK, pkt->Copy(), 0);
        return true;
    }

    void
    OstNode::SpwReadyHandler()
    {
        if(ports.size())
            Simulator::ScheduleNow(&OstSocket::peek_from_transmit_fifo, ports[0]);
        return;
    }

    std::string
    OstNode::GetSegmentTypeName(SegmentFlag t)
    {
        switch (t)
        {
        case ACK:
            return "ACK";
        default:
            return "DATA";
        }
    }

    std::string
    OstNode::GetTransportEventName(TransportLayerEvent e)
    {
        switch (e)
        {
        case PACKET_ARRIVED_FROM_NETWORK:
            return "PACKET_ARRIVED_FROM_NETWORK";
        case APPLICATION_PACKET_READY:
            return "APPLICATION_PACKET_READY";
        case RETRANSMISSION_INTERRUPT:
            return "RETRANSMISSION_INTERRUPT";
        case SPW_READY:
            return "SPW_READY";
        default:
            return "unknown event";
        }
    }

    int8_t
    OstNode::send_packet(uint8_t address, const uint8_t *buffer, uint32_t size)
    {
        std::cout << "to send\n";
        if (ports.size() == 0 || ports[0]->GetAddress() != address || ports[0]->GetState() != OstSocket::State::OPEN)
            return -1;
        return ports[0]->send(buffer, size);
    }
} // namespace ns3