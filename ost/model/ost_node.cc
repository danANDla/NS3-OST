#include "ost_node.h"
#include "ost_socket.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/spw-channel.h"

#include <stdio.h>
#include <string.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OstNode");

OstNode::OstNode(Ptr<SpWDevice> dev, int8_t mode)
    : tx_window_bottom(0),
      tx_window_top(0),
      rx_window_bottom(0),
      rx_window_top(WINDOW_SZ),
      tx_buffer(std::vector<Ptr<Packet>>(WINDOW_SZ)),
      rx_buffer(std::vector<Ptr<Packet>>(WINDOW_SZ)),
      acknowledged(std::vector<bool>(WINDOW_SZ)),
      queue(TimerFifo(WINDOW_SZ)),
      spw_layer(dev),
      ports(std::vector<Ptr<OstSocket>> (PORTS_NUMBER, nullptr))
{
    queue.set_callback(MakeCallback(&OstNode::hw_timer_handler, this));
    spw_layer->SetReceiveCallback(MakeCallback(&OstNode::network_layer_handler, this));
    spw_layer->GetAddress().CopyTo(&self_address);
}

OstNode::~OstNode()
{
}

int8_t
OstNode::event_handler(const TransportLayerEvent e)
{
    NS_LOG_LOGIC("NODE[" << std::to_string(self_address) << "] event: " << event_name(e) << " tx: "
                         << std::to_string(tx_window_bottom) << " " << std::to_string(tx_window_top)
                         << ", rx: " << std::to_string(rx_window_bottom) << " "
                         << std::to_string(rx_window_top) << " ");

    switch (e)
    {
    case PACKET_ARRIVED_FROM_NETWORK:
        get_packet_from_physical();
        break;
    case APPLICATION_PACKET_READY:
        send_to_physical(DTA, (tx_window_top + MAX_UNACK_PACKETS - 1) % MAX_UNACK_PACKETS);
        break;
    case RETRANSMISSION_INTERRUPT:
        send_to_physical(DTA, to_retr);
        break;
    default:
        break;
    }

    return 0;
}

int8_t
OstNode::add_packet_to_tx(Ptr<Packet> p)
{
    if (tx_sliding_window_have_space())
    {
        acknowledged[tx_window_top] = 0;

        tx_buffer[tx_window_top] = p;
        OstHeader header = OstHeader(tx_window_top, self_address, p->GetSize());
        tx_buffer[tx_window_top]->AddHeader(header);
        tx_window_top = (tx_window_top + 1) % WINDOW_SZ;
        return 1;
    }
    return -1;
}

void
OstNode::SetReceiveCallback(ReceiveCallback cb)
{
    rx_cb = cb;
}

void
OstNode::start()
{
    spw_layer->ErrorResetSpWState();
}

void
OstNode::shutdown()
{
    spw_layer->Shutdown();
}

void
OstNode::add_packet_to_rx(Ptr<Packet> p)
{
    rx_buffer[rx_window_bottom] = p;
}


int8_t
OstNode::open_connection(uint8_t addr)
{
    if (addr == self_address)
        return -1;

    Ptr<OstSocket> sk;
    int8_t r = get_socket(addr, sk);
    if (r != 1) // create new
    {
        int8_t r = aggregate_socket(addr);
        if (r != -1)
            ports[r]->open(OstSocket::Mode::CONNECTIONLESS);
    }
    else
    {
        if (sk->get_state() == OstSocket::OPEN)
            return 0; // already opened
        sk->open(OstSocket::Mode::CONNECTIONLESS);
    }
    return 1;
}

int8_t
OstNode::close_connection(uint8_t addr)
{
    if(addr == self_address)
        return -1;

    for (int i = 0; i < PORTS_NUMBER; ++i)
    {
        if (ports[i]->get_address() == addr)
        {
            ports[i]->close();
            return 1;
        }
    }
    return 0;
}

int8_t
OstNode::send_packet(uint8_t address, const uint8_t * buffer, uint32_t size)
{
    if(size > MAX_SPW_PACKET_SZ) return -2;

    Ptr<Packet> p = Create<Packet>(buffer, size);
    if (tx_sliding_window_have_space())
    {
        acknowledged[tx_window_top] = false;
        tx_buffer[tx_window_top] = p;
        tx_window_top = (tx_window_top + 1) % MAX_UNACK_PACKETS;
        return 0;
    }
    NS_LOG_ERROR("sliding window run out of space");
    return -1;
}

int8_t
OstNode::receive_packet(const uint8_t* buffer, uint32_t& received_sz)
{
    return -1;
}

int8_t
OstNode::get_socket(uint8_t addr, Ptr<OstSocket>& sk)
{
    for (int i = 0; i < PORTS_NUMBER; ++i)
    {
        if (ports[i]->get_address() == addr)
        {
            sk = ports[i];
            return 1;
        }
    }
    return -1;
}

int8_t
OstNode::aggregate_socket(uint8_t address)
{
    for (int i = 0; i < PORTS_NUMBER; ++i)
    {
        if (!ports[i] || ports[i]->get_address() == address)
        {
            ports[i] = Create<OstSocket>(this, address, self_address, i);
            ports[i]->set_address(address);
            return i;
        }
    }
    return -1;
}

int8_t
OstNode::delete_socket(uint8_t address)
{
    for (int i = 0; i < PORTS_NUMBER; ++i)
    {
        if (ports[i]->get_address() == address)
        {
            return 1;
        }
    }
    return -1;
}

int8_t close();
uint8_t get_address();

Ptr<SpWDevice>
OstNode::GetSpWLayer()
{
    return spw_layer;
}

void
OstNode::send_to_application(Ptr<Packet> packet)
{
    rx_cb(self_address, packet);
};

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
OstNode::send_to_physical(SegmentFlag t, uint8_t seq_n)
{
    OstHeader header;
    if (t == ACK)
    {
        Ptr<Packet> p = Create<Packet>();
        header.set_payload_len(0);
        header.set_seq_number(seq_n);
        header.set_flag(ACK);
        header.set_src_addr(-1);
        p->AddHeader(header);
        spw_layer->Send(p, spw_layer->GetBroadcast(), 0);
    }
    else
    {
        Ptr<Packet> p = tx_buffer[seq_n]->Copy();
        header.set_payload_len(p->GetSize());
        header.set_seq_number(seq_n);
        header.set_flag(DTA);
        header.set_src_addr(-1);
        p->AddHeader(header);
        spw_layer->Send(p, spw_layer->GetBroadcast(), 0);
        if (queue.add_new_timer(seq_n, DURATION_RETRANSMISSON) != 0)
            NS_LOG_ERROR("timer error");
    }
    return 0;
}

int8_t
OstNode::get_packet_from_physical()
{
    OstHeader header;
    rx_buffer[rx_window_bottom]->RemoveHeader(header);

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

bool
OstNode::hw_timer_handler(uint8_t seq_n)
{
    to_retr = seq_n;
    event_handler(RETRANSMISSION_INTERRUPT);
    return true;
}

bool
OstNode::network_layer_handler(Ptr<NetDevice> dev,
                               Ptr<const Packet> pkt,
                               uint16_t mode,
                               const Address& sender)
{
    add_packet_to_rx(pkt->Copy());
    Simulator::ScheduleNow(&OstNode::event_handler, this, PACKET_ARRIVED_FROM_NETWORK);
    return true;
}


std::string
OstNode::segment_type_name(SegmentFlag t)
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
} // namespace ns3
