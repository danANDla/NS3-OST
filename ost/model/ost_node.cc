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
    : tx_window_bottom(0),
      tx_window_top(0),
      rx_window_bottom(0),
      rx_window_top(WINDOW_SZ),
      tx_buffer(std::vector<Ptr<Packet>>(MAX_SEQ_N)),
      rx_buffer(std::vector<Ptr<Packet>>(MAX_SEQ_N)),
      transmit_fifo(CreateObject<DropTailQueue<Packet>>()),
      receive_fifo(CreateObject<DropTailQueue<Packet>>()),
      acknowledged(std::vector<bool>(MAX_SEQ_N, false)),
      received(std::vector<bool>(MAX_SEQ_N, false)),
      queue(TimerFifo()),
      spw_layer(dev),
      ports(std::vector<Ptr<OstSocket>> (PORTS_NUMBER, nullptr))
{
    queue.set_callback(MakeCallback(&OstNode::hw_timer_handler, this));
    spw_layer->SetReceiveCallback(MakeCallback(&OstNode::network_layer_handler, this));
    spw_layer->GetAddress().CopyTo(&self_address);
    spw_layer->SetPacketSentCallcback(MakeCallback(&OstNode::packet_sent_handler, this));
    spw_layer->SetDeviceReadyCallback(MakeCallback(&OstNode::spw_ready_handler, this));
}

OstNode::~OstNode()
{
}

int8_t
OstNode::event_handler(const TransportLayerEvent e)
{
    switch (e)
    {
    case PACKET_ARRIVED_FROM_NETWORK:
    case APPLICATION_PACKET_READY:
    case RETRANSMISSION_INTERRUPT:
        NS_LOG_INFO("NODE[" << std::to_string(self_address) << "] event: " << event_name(e) << " tx: "
                            << std::to_string(tx_window_bottom) << " " << std::to_string(tx_window_top)
                            << ", rx: " << std::to_string(rx_window_bottom) << " "
                            << std::to_string(rx_window_top) << " ");
    default:
        break;
    }

    switch (e)
    {
    case PACKET_ARRIVED_FROM_NETWORK:
        get_packet_from_physical();
        break;
    case APPLICATION_PACKET_READY:
        send_to_physical(DTA, (tx_window_top + MAX_SEQ_N - 1) % MAX_SEQ_N);
        break;
    case RETRANSMISSION_INTERRUPT:
        send_to_physical(DTA, to_retr);
        break;
    case SPW_READY:
        peek_from_transmit_fifo();
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
        OstHeader header;
        p->RemoveHeader(header);
        header.set_seq_number(tx_window_top);
        p->AddHeader(header);
        tx_buffer[tx_window_top] = p->Copy();
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
    OstHeader header = OstHeader(0, address, size);
    header.set_flag(DTA);
    p->AddHeader(header);
    add_packet_to_transmit_fifo(p->Copy());

    if(spw_layer->IsReadyToTransmit()) 
    {
        Simulator::ScheduleNow(&OstNode::peek_from_transmit_fifo, this);
        return 1;
    }
    return 0;
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

Ptr<SpWDevice>
OstNode::GetSpWLayer()
{
    return spw_layer;
}

uint8_t 
OstNode::get_address() const {return self_address;} 

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
        header.set_src_addr(self_address);
        p->AddHeader(header);
        bool r = spw_layer->Send(p, spw_layer->GetBroadcast(), 0);
        return r ? 1: 0;
    }
    else if (t==DTA)
    {
        Ptr<Packet> p = tx_buffer[seq_n]->Copy();
        p->PeekHeader(header);
        if(!header.is_dta())
        {
            NS_LOG_ERROR("wrong type of packet was in tx_buffer");
            return -1;
        }

        bool r = spw_layer->Send(p, spw_layer->GetBroadcast(), seq_n);
        if (queue.add_new_timer(seq_n, DURATION_RETRANSMISSON) != 0) {
            NS_LOG_ERROR("timer error");
            return -1;
        }
        return r ? 1: 0;
    }
    return 0;
}

int8_t
OstNode::get_packet_from_physical()
{
    OstHeader header;
    Ptr<Packet> pk = receive_fifo->Dequeue();
    pk->RemoveHeader(header);
    uint8_t seq_n = header.get_seq_number();
    if (header.is_ack() ||in_tx_window(seq_n))
    {
        mark_packet_ack(seq_n);
    }
    else
    {
        if (in_rx_window(seq_n))
        {
            mark_packet_receipt(seq_n, pk);
        }
        return send_to_physical(ACK, seq_n);
    }
    return 1;
}

void
OstNode::peek_from_transmit_fifo()
{
    if(!transmit_fifo->IsEmpty() && tx_sliding_window_have_space())
    {
        Ptr<Packet> p = transmit_fifo->Dequeue();
        add_packet_to_tx(p->Copy());
        Simulator::ScheduleNow(&OstNode::event_handler, this, APPLICATION_PACKET_READY);
    }
    return;
}

int8_t
OstNode::mark_packet_ack(uint8_t seq_n)
{
    if (!acknowledged[seq_n])
    {
        acknowledged[seq_n] = true;
        queue.cancel_timer(seq_n);
        while ((tx_window_bottom + 1) % MAX_SEQ_N <= tx_window_top && acknowledged[tx_window_bottom])
        {
            tx_window_bottom = (tx_window_bottom + 1) % MAX_SEQ_N;
            // and dealloc mem for packet
        }
    }
    return 1;
}

int8_t
OstNode::mark_packet_receipt(uint8_t seq_n, Ptr<Packet> pkt)
{
    if (!received[seq_n])
    {
        received[seq_n] = true;
        rx_buffer[seq_n] = pkt->Copy();
        while (received[rx_window_bottom])
        {
            send_to_application(rx_buffer[rx_window_bottom]);
            received[rx_window_top] = false;
            rx_window_bottom = (rx_window_bottom + 1) % MAX_SEQ_N;
            rx_window_top = (rx_window_top + 1) % MAX_SEQ_N;
            // and dealloc mem for packet
        }
    }
    return 1;
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
    NS_LOG_LOGIC("check for in_rx_window " << std::to_string(seq_n) << " rx_window_bot = " << std::to_string(rx_window_bottom) << ", rx_window_top" << std::to_string(rx_window_top) << "\n");
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
    add_packet_to_receive_fifo(pkt->Copy());
    Simulator::ScheduleNow(&OstNode::event_handler, this, PACKET_ARRIVED_FROM_NETWORK);
    return true;
}

void
OstNode::add_packet_to_transmit_fifo(Ptr<Packet> p)
{
    transmit_fifo->Enqueue(p);
}


void
OstNode::add_packet_to_receive_fifo(Ptr<Packet> p)
{
    receive_fifo->Enqueue(p);
}

void
OstNode::packet_sent_handler(uint8_t seq_n, bool dta)
{
    if(dta)
    {
    }
    peek_from_transmit_fifo();
    return;
}

void
OstNode::spw_ready_handler()
{
   Simulator::ScheduleNow(&OstNode::event_handler, this, SPW_READY); 
   return;
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
    case SPW_READY:
        return "SPW_READY";
    case PACKET_SENT:
        return "PACKET_SENT";
    default:
        return "unknown event";
    }
}
} // namespace ns3
