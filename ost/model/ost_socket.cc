#include "ost_socket.h"

#include "ost_node.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include <cstdio>

namespace ns3
{

    NS_LOG_COMPONENT_DEFINE("OstSocket");

    int8_t
    OstSocket::socket_event_handler(Event e, Ptr<Packet> seg, uint8_t seq_n)
    {
        switch (e)
        {
        case PACKET_ARRIVED_FROM_NETWORK:
            return segment_arrival_event_socket_handler(seg);
        case APPLICATION_PACKET_READY:
            return send_to_physical(DTA, (tx_window_top + MAX_SEQ_N - 1) % MAX_SEQ_N);
        case RETRANSMISSION_INTERRUPT:
            return send_to_physical(DTA, seq_n);
        case SPW_READY:
            peek_from_transmit_fifo();
            return 1;
        default:
            return -1;
        }
        return 1;
    }

    OstSocket::OstSocket(Ptr<OstNode> parent)
        : ost(parent),
            state(State::CLOSED),
            to_address(parent->GetAddress()),
            self_port(0),
            tx_window_bottom(0),
            tx_window_top(0),
            rx_window_bottom(0),
            rx_window_top(WINDOW_SZ),
            transmit_fifo(CreateObject<DropTailQueue<Packet>>()),
            receive_fifo(CreateObject<DropTailQueue<Packet>>()),
            tx_window(std::vector<Ptr<Packet>>(WINDOW_SZ)),
            rx_window(std::vector<Ptr<Packet>>(WINDOW_SZ)),
            acknowledged(std::vector<bool>(WINDOW_SZ)),
            received(std::vector<bool>(WINDOW_SZ)),
            queue(Create<TimerFifo>()),
            aggregated(false)
            {
                queue->set_callback(MakeCallback(&OstSocket::timer_handler, this));
            };

    int8_t
    OstSocket::open(OstSocket::Mode sk_mode)
    {
        mode = sk_mode;
        if (mode == CONNECTIONLESS)
        {
            state = OPEN;
            spw_layer = ost->GetSpWLayer();
            queue->init_hw_timer();

            char buff[200];
            sprintf(buff, "opened socket [%d:%d] to %d\n", ost->GetAddress(), self_port, to_address);
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
        NS_LOG_INFO("Closing socket [" << std::to_string(ost->GetAddress()) << ":" << std::to_string(self_port) << "] -> [" << std::to_string(to_address) << "]");
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

        return 1;
    }

    int8_t
    OstSocket::send(const uint8_t *buffer, uint32_t size)
    {
        char buff[200];
        if (state != OPEN)
        {
            sprintf(buff, "socket[%d:%d] must be in open state\n", ost->GetAddress(), self_port);
            NS_LOG_ERROR(buff);
            return -1;
        }

        if (size > 1024 * 64)
            return -2;
        Ptr<Packet> p = Create<Packet>(buffer, size);
        OstHeader* header = new OstHeader(0, ost->GetAddress(), size);
        header->set_flag(DTA);
        header->set_payload_len(size);
        header->set_src_addr(ost->GetAddress());
        std::cout << "header is " << std::to_string(header->get_payload_len()) << " , size is " << std::to_string(size)  <<  " \n";
        p->AddHeader(*header);
        transmit_fifo->Enqueue(p->Copy());
        if (spw_layer->IsReadyToTransmit())
        {
            Simulator::ScheduleNow(&OstSocket::peek_from_transmit_fifo, this);
            return 1;
        }
        return 0;
    }

    int8_t
    OstSocket::receive(Ptr<Packet> &segment)
    {
    }

    void
    OstSocket::peek_from_transmit_fifo()
    {
        NS_LOG_INFO("NODE[" << std::to_string(ost->GetAddress()) << "] peeking from fifo, in queue " << std::to_string(transmit_fifo->GetCurrentSize().GetValue()) << " packets\n");
        if (!transmit_fifo->IsEmpty() && tx_sliding_window_have_space())
        {
            Ptr<const Packet> p = transmit_fifo->Peek();
            if (add_packet_to_tx(p->Copy()) != -1)
            {
                Simulator::ScheduleNow(&OstSocket::socket_event_handler, this, APPLICATION_PACKET_READY, nullptr, 0);
                transmit_fifo->Dequeue();
                if (!transmit_fifo->IsEmpty())
                    Simulator::Schedule(MicroSeconds(10), &OstSocket::peek_from_transmit_fifo, this);
            }
        }
    }

    int8_t
    OstSocket::add_packet_to_tx(Ptr<Packet> p)
    {
        if (tx_sliding_window_have_space())
        {
            OstHeader header;
            p->RemoveHeader(header);
            header.set_seq_number(tx_window_top);
            p->AddHeader(header);
            tx_window[tx_window_top] = p->Copy();
            tx_window_top = (tx_window_top + 1) % MAX_SEQ_N;
            return 1;
        }
        return -1;
    }

    void
    OstSocket::add_packet_to_transmit_fifo(Ptr<Packet> p)
    {
        transmit_fifo->Enqueue(p);
    }

    uint8_t
    OstSocket::GetAddress() const
    {
        return to_address;
    }

    void
    OstSocket::SetAddress(uint8_t addr)
    {
        std::cout << "setting address " << std::to_string(addr)  << "\n";
        to_address = addr;
    }

    OstSocket::State
    OstSocket::GetState() const
    {
        return state;
    }

    void
    OstSocket::SetReceiveCallback(OstSocket::ReceiveCallback cb)
    {
        application_receive_callback = cb;
    }

    bool
    OstSocket::IsAggregated() const
    {
        return aggregated;
    }

    void
    OstSocket::SetAggregated(bool aggr) 
    {
        aggregated = aggr;
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
                if (in_tx_window(header.get_seq_number()) && !acknowledged[header.get_seq_number()])
                {
                    mark_packet_ack(header.get_seq_number());
                }
            }
            else
            {
                if (in_rx_window(header.get_seq_number()) && !received[header.get_seq_number()])
                {
                    mark_packet_receipt(header.get_seq_number(), seg);
                }
                send_to_physical(ACK, header.get_seq_number());
            }
            return 1;
        }
        else
        {
            full_states_handler(seg);
        }
        return -1;
    }

    int8_t
    OstSocket::mark_packet_ack(uint8_t seq_n)
    {
        if (!acknowledged[seq_n])
        {
            acknowledged[seq_n] = true;
            if (queue->cancel_timer(seq_n) != 1)
            {
                NS_LOG_ERROR("error removing timer from queue");
            };
            while (acknowledged[tx_window_bottom] && (tx_window_bottom + 1) % MAX_SEQ_N <= tx_window_top)
            {
                tx_window_bottom = (tx_window_bottom + 1) % MAX_SEQ_N;
                // and dealloc mem for packet
            }
            peek_from_transmit_fifo();
        }
        return 1;
    }

    int8_t
    OstSocket::mark_packet_receipt(uint8_t seq_n, Ptr<Packet> pkt)
    {
        if (!received[seq_n])
        {
            received[seq_n] = true;
            rx_window[seq_n] = pkt->Copy();
            while (received[rx_window_bottom])
            {
                send_to_application(rx_window[rx_window_bottom]);
                received[rx_window_top] = false;
                rx_window_bottom = (rx_window_bottom + 1) % MAX_SEQ_N;
                rx_window_top = (rx_window_top + 1) % MAX_SEQ_N;
                // and dealloc mem for packet
            }
        }
        return 1;
    }

    void
    OstSocket::send_rejection(uint8_t seq_n) {};

    void
    OstSocket::send_syn(uint8_t seq_n) {};

    void
    OstSocket::send_syn_confirm(uint8_t seq_n) {};

    void
    OstSocket::send_confirm(uint8_t seq_n) {};

    int8_t
    OstSocket::send_to_physical(SegmentFlag f, uint8_t seq_n)
    {
        if (f == ACK)
        {
            OstHeader header;
            header.set_payload_len(0);
            header.set_seq_number(seq_n);
            header.set_flag(ACK);
            header.set_src_addr(ost->GetAddress());
            Ptr<Packet> ack_packet = Create<Packet>(0);
            ack_packet->AddHeader(header);
            send_spw(ack_packet);
        }
        else
        {
            OstHeader header;
            tx_window[seq_n]->PeekHeader(header);
            if (header.get_payload_len() == 0 || !header.is_dta() || header.get_seq_number() != seq_n ||
                header.get_src_addr() != ost->GetAddress())
            {
                NS_LOG_ERROR("trying transmit wrong packet from buffer\n");
                return -1;
            }
            send_spw(tx_window[seq_n]);
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

    void
    OstSocket::start_close_wait_timer() {};

    void
    OstSocket::stop_close_wait_timer() {};

    void
    OstSocket::dealloc() {};

    int8_t
    OstSocket::add_to_rx(Ptr<Packet> segment)
    {
        return -1;
    }

    void
    OstSocket::send_to_application(Ptr<Packet> packet)
    {
        // application_receive_callback(ost->GetAddress(), self_port, packet);
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
    OstSocket::GetStateName(State s)
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
    OstSocket::full_states_handler(Ptr<Packet> p)
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
                    // send_to_application(rx_window[rx_window_bottom]);
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

    bool
    OstSocket::timer_handler(uint8_t seq_n) {
        socket_event_handler(RETRANSMISSION_INTERRUPT, nullptr, seq_n);
    }

    void
    OstSocket::set_state(State s)
    {
        NS_LOG_LOGIC("NODE[" << std::to_string(ost->GetAddress()) << ":" << std::to_string(self_port)
                             << "] new state " << GetStateName(s));
        state = s;
    }

} // namespace ns3
