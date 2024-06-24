
#ifndef OST_SOCKET_H
#define OST_SOCKET_H

#include "timer_fifo.h"

#include "ns3/callback.h"
#include "ns3/object.h"
#include "ns3/ost-header.h"
#include "ns3/ptr.h"
#include "ns3/spw-device.h"
#include "ns3/drop-tail-queue.h"

#include <inttypes.h>

/**
 * \defgroup ost Open SpaceWire Transport Layer Node
 * This section documents the API of the transport-layer node of spw network.
 */
namespace ns3
{

    class OstNode;

    class OstSocket : public Object
    {
    public:
        static const uint16_t MAX_SEQ_N = 256; // in fact range 0..255
        static const uint16_t WINDOW_SZ = 10;
        static const micros_t DURATION_RETRANSMISSON = 2000000; // 2 secs

        typedef enum
        {
            CONNECTIONLESS = 0,
            CONNECTION_ACTIVE,
            CONNECTION_PASSIVE
        } Mode;

        typedef enum
        {
            CLOSED = 0,
            LISTEN,
            SYN_SENT,
            SYN_RCVD,
            OPEN,
            CLOSE_WAIT,
        } State;

        typedef enum
        {
            PACKET_ARRIVED_FROM_NETWORK = 0,
            APPLICATION_PACKET_READY,
            RETRANSMISSION_INTERRUPT,
            SPW_READY
        } Event;

        OstSocket(Ptr<OstNode> parent);
        ~OstSocket(){};

        int8_t open(Mode mode);
        int8_t close();
        int8_t send(const uint8_t * buffer, uint32_t size);
        int8_t receive(Ptr<Packet> &segment);
        int8_t socket_event_handler(const Event e, Ptr<Packet> seg, uint8_t seq_n);
        void add_packet_to_transmit_fifo(Ptr<Packet>);
        void peek_from_transmit_fifo();

        /*
        *  NS-3 Specific
        */
        uint8_t GetAddress() const;
        void SetAddress(uint8_t);
        State GetState() const;
        typedef Callback<void, uint8_t, uint8_t, Ptr<Packet>> ReceiveCallback;
        void SetReceiveCallback(OstSocket::ReceiveCallback cb);
        bool IsAggregated() const;
        void SetAggregated(bool);

    private:
        void init_socket();
        int8_t segment_arrival_event_socket_handler(Ptr<Packet> seg);
        int8_t send_to_physical(SegmentFlag f, uint8_t seg_n);
        void send_spw(Ptr<Packet> segment);
        void send_rejection(uint8_t seq_n);
        void send_syn(uint8_t seq_n);
        void send_syn_confirm(uint8_t seq_n);
        void send_confirm(uint8_t seq_n);
        void start_close_wait_timer();
        void stop_close_wait_timer();
        void dealloc();
        int8_t in_rx_window(uint8_t) const;
        void send_to_application(Ptr<Packet> packet);
        int8_t add_to_rx(Ptr<Packet> seg);
        void add_packet_to_receive_fifo(Ptr<Packet>);
        int8_t mark_packet_receipt(uint8_t seq_n, Ptr<Packet>);
        int8_t in_tx_window(uint8_t) const;
        int8_t tx_sliding_window_have_space() const;
        int8_t add_packet_to_tx(Ptr<Packet> p);
        int8_t mark_packet_ack(uint8_t seq_n);
        int8_t full_states_handler(Ptr<Packet> seg);
        bool timer_handler(uint8_t seq_n);
        void set_state(State);

        Ptr<OstNode> ost;
        Mode mode;
        State state;
        uint8_t to_address;
        uint8_t self_port;
        uint8_t to_retr;
        uint8_t tx_window_bottom;
        uint8_t tx_window_top;
        uint8_t rx_window_bottom;
        uint8_t rx_window_top;
        Ptr<Queue<Packet>> transmit_fifo;
        Ptr<Queue<Packet>> receive_fifo;
        std::vector<Ptr<Packet>> tx_window;
        std::vector<Ptr<Packet>> rx_window;
        std::vector<bool> acknowledged;
        std::vector<bool> received;
        Ptr<TimerFifo> queue;
        Ptr<SpWDevice> spw_layer;
        bool aggregated;

        /*
        *  NS-3 Specific
        */
        std::string GetStateName(State);
        ReceiveCallback application_receive_callback;
    };
} // namespace ns3

#endif