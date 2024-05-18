#include "ost_socket.h"

#include "ost_node.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include <cstdio>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OstSocket");

// int8_t
// OstSocket::open(int8_t mode)
// {
//     if (mode == 1) // active mode
//     {
//         tx_window_bottom = 0;
//         tx_window_top = 0;
//         Ptr<Packet> p = Create<Packet>();
//         send_syn(tx_window_bottom);
//         set_state(State::SYN_SENT);
//     }
//     else
//     {
//         set_state(State::LISTEN);
//     }
// }

int8_t
OstSocket::open(OstSocket::Mode sk_mode)
{
    mode = sk_mode;
    if (mode == CONNECTIONLESS)
    {
        state = OPEN;
        init_socket();
        queue->init_hw_timer();

        char buff[200];
        sprintf(buff, "opened socket [%d:%d] to %d\n", self_address, self_port, to_address);
        NS_LOG_INFO(buff);
    }
    else
    {
        return -1;
    }
}

int8_t
OstSocket::close()
{
    if (mode != CONNECTIONLESS)
    {
        state = CLOSE_WAIT;
        send_rejection(0);
    }
    else
    {
        state = CLOSED;
    }
    tx_window_bottom = 0;
    tx_window_top = 0;
    rx_window_bottom = 0;
    rx_window_top = 0;
    to_retr = 0;
    self_address = -1;
}

int8_t
OstSocket::send(Ptr<Packet> segment)
{
    char buff[200];
    if (state != OPEN)
    {
        sprintf(buff, "socket[%d:%d] must be in open state\n", self_address, self_port);
        NS_LOG_ERROR(buff);
        return -1;
    }

    if (!tx_sliding_window_have_space())
    {
        NS_LOG_ERROR("no free space in tx_buffer\n");
        return -1;
    }

    OstHeader seg_header;
    segment->PeekHeader(seg_header);

    acknowledged[tx_window_top] = 0;
    tx_buffer[tx_window_top] = Create<Packet>(buff, seg_header.get_payload_len());
    OstHeader header = OstHeader(tx_window_top, self_address, seg_header.get_payload_len());
    header.set_flag(DTA);
    tx_buffer[tx_window_top]->AddHeader(header);

    int8_t r = send_to_physical(DTA, tx_window_top);
    if (r != 1)
        return -1;
    tx_window_top = (tx_window_top + 1) % WINDOW_SZ;
    return 1;
}

int8_t
receive(Ptr<Packet>& segment)
{
}

int8_t
OstSocket::add_to_tx(Ptr<Packet> seg, uint8_t& seq_n)
{
    if (tx_sliding_window_have_space())
    {
        acknowledged[tx_window_top] = 0;

        tx_buffer[tx_window_top] = seg;
        OstHeader header = OstHeader(tx_window_top, self_address, seg->GetSize());
        tx_buffer[tx_window_top]->AddHeader(header);
        seq_n = tx_window_top;
        tx_window_top = (tx_window_top + 1) % WINDOW_SZ;
        return 1;
    }
    return -1;
}

int8_t socket_event_handler(const TransportLayerEvent e, Ptr<Packet> seg, uint8_t seq_n);

uint8_t
OstSocket::get_address() const
{
    return self_address;
}

void
OstSocket::set_address(uint8_t addr)
{
    self_address = addr;
}

OstSocket::State
OstSocket::get_state() const
{
    return state;
}

void
OstSocket::SetReceiveCallback(OstSocket::ReceiveCallback cb)
{
    application_receive_callback = cb;
}

void
OstSocket::init_socket()
{
    ;
}

int8_t
OstSocket::segment_arrival_event_socket_handler(Ptr<Packet> seg)
{
    if (mode == CONNECTIONLESS)
    {
        OstHeader header;

        seg->PeekHeader(header);

        if (header.is_ack())
        {
            if (in_tx_window(header.get_seq_number()))
            {
                if (!acknowledged[header.get_seq_number()])
                {
                    acknowledged[header.get_seq_number()] = 1;
                    queue->cancel_timer(header.get_seq_number());
                    while (acknowledged[tx_window_bottom])
                    {
                        // free(tx_buffer[tx_window_bottom].payload);
                        tx_window_bottom = (tx_window_bottom + 1) % WINDOW_SZ;
                    }
                }
            }
        }
        else
        {
            if (in_rx_window(header.get_seq_number()))
            {
                // memcpy(&rx_buffer[header.get_seq_number()], seg, sizeof(OstSegmentHeader) +
                // seg->header.payload_length); add_to_rx(seg);
                send_to_application(rx_buffer[rx_window_bottom]);
                rx_window_bottom = (rx_window_bottom + 1) % WINDOW_SZ;
                rx_window_top = (rx_window_top + 1) % WINDOW_SZ;
            }
            send_to_physical(ACK, header.get_seq_number());
        }
        return 1;
    }
    return -1;
}

void send_rejection(uint8_t seq_n);
void send_syn(uint8_t seq_n);
void send_syn_confirm(uint8_t seq_n);
void send_confirm(uint8_t seq_n);

int8_t
OstSocket::send_to_physical(SegmentFlag f, uint8_t seq_n)
{
    if (f == ACK)
    {
        OstHeader header;
        header.set_payload_len(0);
        header.set_seq_number(0);
        header.set_flag(ACK);
        header.set_src_addr(self_address);
        Ptr<Packet> ack_packet = Create<Packet>();
        ack_packet->AddHeader(header);
        send_spw(ack_packet);
    }
    else
    {
        OstHeader header;
        tx_buffer[seq_n]->PeekHeader(header);
        if (header.get_payload_len() == 0 || !header.is_dta() || header.get_seq_number() != seq_n ||
            header.get_src_addr() != self_address)
        {
            NS_LOG_ERROR("trying transmit wrong packet from buffer\n");
        }
        return -1;
        send_spw(tx_buffer[seq_n]);
        if (queue->add_new_timer(seq_n, DURATION_RETRANSMISSON) != 1)
            return -1;
    }
    return 1;
}

void
OstSocket::send_spw(Ptr<Packet> segment)
{
    Mac8Address addr;
    addr.CopyFrom(&to_address);
    spw_layer->Send(segment, addr, 0);
}

void start_close_wait_timer();
void stop_close_wait_timer();
void dealloc();

void
OstSocket::send_to_application(Ptr<Packet> packet)
{
    application_receive_callback(self_address, self_port, packet);
}

int8_t
OstSocket::in_tx_window(uint8_t seq_n) const
{
    return (tx_window_top >= tx_window_bottom && seq_n >= tx_window_bottom &&
            seq_n < tx_window_top) ||
           (tx_window_bottom > tx_window_top &&
            (seq_n >= tx_window_bottom || seq_n < tx_window_top));
}

int8_t
OstSocket::in_rx_window(uint8_t seq_n) const
{
    return (rx_window_top >= rx_window_bottom && seq_n >= rx_window_bottom &&
            seq_n < rx_window_top) ||
           (rx_window_bottom > rx_window_top &&
            (seq_n >= rx_window_bottom || seq_n < rx_window_top));
}

int8_t 
OstSocket::tx_sliding_window_have_space() const
{
    if (tx_window_top >= tx_window_bottom)
    {
        return tx_window_top - tx_window_bottom < WINDOW_SZ;
    }
    else
    {
        return WINDOW_SZ - tx_window_top + 1 + tx_window_bottom < WINDOW_SZ;
    }
}

std::string
OstSocket::state_name(State s)
{
    switch (s)
    {
    case CLOSED:
        return "CLOSED";
    case SYN_SENT:
        return "SYN-SENT";
    case SYN_RCVD:
        return "SYN-RCVD";
    case LISTEN:
        return "LISTEN";
    case OPEN:
        return "OPEN";
    case CLOSE_WAIT:
        return "CLOSE-WAIT";
    default:
        return "bad";
    }
}

int8_t
OstSocket::segment_arrival_event_socket_handler(Ptr<Packet> p)
{
    OstHeader hd;
    p->RemoveHeader(hd);
    switch (state)
    {
    case State::CLOSED:
        if (hd.is_rst())
            ;
        else
            send_rejection(0);
        break;
    case State::CLOSE_WAIT:
        if (hd.is_rst())
        {
            set_state(State::CLOSED);
            stop_close_wait_timer();
            dealloc();
        }
        break;
    case State::LISTEN:
        if (hd.is_ack() or hd.is_dta())
            send_rejection(0);
        else if (hd.is_syn())
        {
            tx_window_bottom = hd.get_seq_number();
            tx_window_top = hd.get_seq_number();
            rx_window_bottom = hd.get_seq_number();
            rx_window_top = WINDOW_SZ - 1;
            send_syn_confirm(hd.get_seq_number());
            set_state(State::SYN_RCVD);
        }
        break;
    case State::SYN_SENT:
        if (hd.is_syn())
        {
            tx_window_bottom = hd.get_seq_number();
            tx_window_top = hd.get_seq_number();
            rx_window_bottom = hd.get_seq_number();
            rx_window_top = WINDOW_SZ - 1;
            if (hd.is_ack())
            {
                send_confirm(hd.get_seq_number());
                set_state(State::OPEN);
            }
            else
            {
                send_syn_confirm(hd.get_seq_number());
                set_state(State::SYN_RCVD);
            }
        }
        else if (hd.is_rst())
        {
            set_state(State::CLOSED);
            dealloc();
        }
        else if (hd.is_ack())
        {
            if (hd.get_seq_number() != tx_window_bottom)
            {
                send_rejection(0);
                start_close_wait_timer();
                set_state(State::CLOSE_WAIT);
            }
        }
    case State::SYN_RCVD:
        if (hd.is_rst())
        {
            if (mode)
            {
                set_state(State::CLOSED);
                dealloc();
            }
            else
            {
                set_state(State::LISTEN);
            }
        }
        else if (hd.is_syn() || hd.is_dta())
        {
            send_rejection(0);
            start_close_wait_timer();
            set_state(State::CLOSE_WAIT);
        }
        else if (hd.is_ack())
        {
            if (hd.get_seq_number() == tx_window_bottom)
            {
                set_state(State::OPEN);
            }
            else
            {
                send_rejection(0);
                start_close_wait_timer();
                set_state(State::CLOSE_WAIT);
            }
        }
        break;
    case State::OPEN:
        if (hd.is_rst())
        {
            start_close_wait_timer();
            set_state(State::CLOSE_WAIT);
        }
        else if (hd.is_syn())
        {
            send_rejection(0);
            start_close_wait_timer();
            set_state(State::CLOSE_WAIT);
        }
        else if (hd.is_ack())
        {
            if (in_tx_window(hd.get_seq_number()))
            {
                if (!acknowledged[hd.get_seq_number()])
                {
                    acknowledged[hd.get_seq_number()] = true;
                    queue->cancel_timer(hd.get_seq_number());
                    while (acknowledged[tx_window_bottom])
                    {
                        tx_window_bottom = (tx_window_bottom + 1) % MAX_UNACK_PACKETS;
                    }
                }
            }
        }
        else if (hd.is_dta())
        {
            if (in_rx_window(hd.get_seq_number()))
            {
                send_to_application(rx_buffer[rx_window_bottom]);
                rx_window_bottom = (rx_window_bottom + 1) % MAX_UNACK_PACKETS;
                rx_window_top = (rx_window_top + 1) % MAX_UNACK_PACKETS;
            }
            send_confirm(hd.get_seq_number());
        }
        break;
    default:
        break;
    }
    return 0;
}

void
OstSocket::set_state(State s)
{
    NS_LOG_LOGIC("NODE[" << std::to_string(self_address) << ":" << std::to_string(self_port)
                         << "] new state " << state_name(s));
    state = s;
}

} // namespace ns3
