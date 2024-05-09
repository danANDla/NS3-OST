#include "ost_node.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/spw-channel.h"

#include <stdio.h>
#include <string.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OstNode");

OstNode::OstNode(uint8_t id, Ptr<SpWDevice> dev)
    : tx_window_bottom(0),
      tx_window_top(0),
      rx_window_bottom(0),
      rx_window_top(WINDOW_SZ),
      tx_buffer(std::vector<Ptr<Packet>>(WINDOW_SZ)),
      rx_buffer(std::vector<Ptr<Packet>>(WINDOW_SZ)),
      acknowledged(std::vector<bool>(WINDOW_SZ)),
      queue(TimerFifo(WINDOW_SZ)),
      spw_layer(dev),
      simulator_id(id)
{
    queue.set_callback(MakeCallback(&OstNode::hw_timer_handler, this));
    spw_layer->SetReceiveCallbackWithSeqN(MakeCallback(&OstNode::network_layer_handler, this));
}

OstNode::~OstNode()
{
}

void
OstNode::SetReceiveCallback(ReceiveCallback cb)
{
    rx_cb = cb;
}

void
OstNode::add_packet_to_rx(Ptr<Packet> p)
{
    rx_buffer[rx_window_bottom] = p;
}

bool
OstNode::tx_sliding_window_have_space()
{
    if (tx_window_top >= tx_window_bottom)
    {
        return tx_window_top - tx_window_bottom < WINDOW_SZ;
    }
    else
    {
        return MAX_UNACK_PACKETS - tx_window_top + 1 + tx_window_bottom < WINDOW_SZ;
    }
}

int8_t
OstNode::add_packet_to_tx(Ptr<Packet> p)
{
    if (tx_sliding_window_have_space())
    {
        acknowledged[tx_window_top] = false;
        tx_buffer[tx_window_top] = p;
        tx_window_top = (tx_window_top + 1) % MAX_UNACK_PACKETS;
        return 0;
    }
    NS_LOG_ERROR("sliding window run out of space");
    return 1;
}

void
OstNode::send_to_application(Ptr<Packet> packet)
{
    rx_cb(simulator_id, packet);
};

void
print_transmission(int sender, int receiver, uint8_t seq_n, bool isAck, uint32_t ch_packet_seq_n, bool isReceiption) {
    if(sender % 2 == 1) {
        NS_LOG_INFO("         NODE[" << std::to_string(sender) << "] --" << std::to_string(ch_packet_seq_n) << "-> <SEQ.N=" << std::to_string(seq_n) << ">" << (isAck ? "<ACK>": "     ") << " NODE[" << std::to_string(receiver) << "] " << (isReceiption?"received":""));
    } else {
        NS_LOG_INFO((isReceiption?"received":"        ") << " NODE[" << std::to_string(receiver) << "] <-" << std::to_string(ch_packet_seq_n) << "-- <SEQ.N=" << std::to_string(seq_n) << (isAck ? "><ACK>": ">     ") << " NODE[" << std::to_string(sender) << "]");
    }
}

int8_t
OstNode::send_to_physical(SegmentType t, uint8_t seq_n)
{
    OstHeader header;
    uint32_t ch_packet_seq_n;
    if (t == DATA)
    {
        Ptr<Packet> p = tx_buffer[seq_n]->Copy();
        header.set_payload_len(p->GetSize());
        header.set_seq_number(seq_n);
        header.set_type(DATA);
        header.set_src_addr(-1);
        p->AddHeader(header);
        ch_packet_seq_n = spw_layer->GetSpWChannel()->IncCntPackets();
        spw_layer->Send(p, spw_layer->GetBroadcast(), 0);
        queue.add_new_timer(tx_window_bottom, 100);
    }
    else if (t == ACK)
    {
        Ptr<Packet> p = Create<Packet>();
        header.set_payload_len(0);
        header.set_seq_number(seq_n);
        header.set_type(ACK);
        header.set_src_addr(-1);
        p->AddHeader(header);
        ch_packet_seq_n = spw_layer->GetSpWChannel()->IncCntPackets();
        spw_layer->Send(p, spw_layer->GetBroadcast(), 0);
    }
    print_transmission(simulator_id, simulator_id % 2 + 1, seq_n, t == ACK, ch_packet_seq_n, false);
    return 0;
}

bool
OstNode::in_tx_window(uint8_t seq_n)
{
    return (tx_window_top >= tx_window_bottom && seq_n >= tx_window_bottom &&
            seq_n < tx_window_top) ||
           (tx_window_bottom > tx_window_top &&
            (seq_n >= tx_window_bottom || seq_n < tx_window_top));
}

bool
OstNode::in_rx_window(uint8_t seq_n)
{
    return (rx_window_top >= rx_window_bottom && seq_n >= rx_window_bottom &&
            seq_n < rx_window_top) ||
           (rx_window_bottom > rx_window_top &&
            (seq_n >= rx_window_bottom || seq_n < rx_window_top));
}

int8_t
OstNode::get_packet_from_physical(uint32_t ns3_ch_packet_seq_n)
{
    OstHeader header;
    rx_buffer[rx_window_bottom]->RemoveHeader(header);

    print_transmission(simulator_id % 2 + 1, simulator_id, header.get_seq_number(), header.is_ack(), ns3_ch_packet_seq_n, true);
    if (header.is_ack())
    {
        if (in_tx_window(header.get_seq_number()))
        {
            if (!acknowledged[header.get_seq_number()])
            {
                acknowledged[header.get_seq_number()] = true;
                queue.cancel_timer(header.get_seq_number());
                while (acknowledged[tx_window_bottom])
                {
                    tx_window_bottom = (tx_window_bottom + 1) % MAX_UNACK_PACKETS;
                }
            }
        }
    }
    else
    {
        if (in_rx_window(header.get_seq_number()))
        {
            send_to_application(rx_buffer[rx_window_bottom]);
            rx_window_bottom = (rx_window_bottom + 1) % MAX_UNACK_PACKETS;
            rx_window_top = (rx_window_top + 1) % MAX_UNACK_PACKETS;
        }
        send_to_physical(ACK, header.get_seq_number());
    }
    return 0;
}


int8_t
OstNode::get_packet_from_application()
{
    uint8_t* buff = new uint8_t[200];
    uint8_t t = 1;
    buff[0] = 1;
    for (int i = 0; i < 129; ++i)
    {
        buff[i] = t * (t + 1);
        t += buff[i];
    }
    return 0;
}

int8_t
OstNode::event_handler(const TransportLayerEvent e, uint32_t ch_packet_seq_n)
{
    NS_LOG_LOGIC("NODE[" << std::to_string(simulator_id) << "] event: " << event_name(e));
    NS_LOG_LOGIC("tx: " << std::to_string(tx_window_bottom) << " " << std::to_string(tx_window_top)
                        << ", rx: " << std::to_string(rx_window_bottom) << " "
                        << std::to_string(rx_window_top) << " ");

    switch (e)
    {
    case PACKET_ARRIVED_FROM_NETWORK:
        get_packet_from_physical(ch_packet_seq_n);
        break;
    case APPLICATION_PACKET_READY:
        send_to_physical(DATA, (tx_window_top + MAX_UNACK_PACKETS - 1) % MAX_UNACK_PACKETS);
        break;
    case RETRANSMISSION_INTERRUPT:
        send_to_physical(DATA, to_retr);
        break;
    default:
        break;
    }

    return 0;
}

bool
OstNode::hw_timer_handler(uint8_t seq_n)
{
    to_retr = seq_n;
    event_handler(RETRANSMISSION_INTERRUPT, 0);
    return true;
}

bool
OstNode::network_layer_handler(Ptr<NetDevice> dev,
                               Ptr<const Packet> pkt,
                               uint32_t ch_packet_seq_n,
                               const Address& sender)
{
    add_packet_to_rx(pkt->Copy());
    Simulator::ScheduleNow(&OstNode::event_handler, this, PACKET_ARRIVED_FROM_NETWORK, ch_packet_seq_n);
    return true;
}

std::string
OstNode::segment_type_name(SegmentType t)
{
    switch (t)
    {
    case ACK:
        return "ACK";
    case DATA:
        return "DATA";
    default:
        return "unknown type";
    }
}

std::string
OstNode::event_name(TransportLayerEvent e)
{
    switch (e)
    {
    case PACKET_ARRIVED_FROM_NETWORK:
        return "PACKET_ARRIVED_FROM_NETWORK";
    case APPLICATION_PACKET_READY:
        return "APPLICATION_PACKET_READY";
    case TRANSPORT_CLK_INTERRUPT:
        return "TRANSPORT_CLK_INTERRUPT";
    case RETRANSMISSION_INTERRUPT:
        return "RETRANSMISSION_INTERRUPT";
    default:
        return "unknown event";
    }
}
}