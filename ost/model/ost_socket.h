
#ifndef OST_SOCKET_H
#define OST_SOCKET_H

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

class OstNode;

class OstSocket : public Object
{
  public:
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
        TRANSPORT_CLK_INTERRUPT,
    } Event;

    OstSocket(Ptr<OstNode> parent, uint8_t to_addr, uint8_t self_addr, uint8_t self_port)
        : ost(parent),
          state(State::CLOSED),
          to_address(to_addr),
          self_address(self_addr),
          self_port(self_port),
          tx_window_bottom(0),
          tx_window_top(0),
          rx_window_bottom(0),
          rx_window_top(WINDOW_SZ),
          tx_buffer(std::vector<Ptr<Packet>>(WINDOW_SZ)),
          rx_buffer(std::vector<Ptr<Packet>>(WINDOW_SZ)),
          acknowledged(std::vector<bool>(WINDOW_SZ)),
          queue(Create<TimerFifo>(WINDOW_SZ)){};
    ~OstSocket() {};

    int8_t open(Mode mode);
    int8_t close();
    int8_t send(Ptr<Packet> segment);
    int8_t receive(Ptr<Packet>& segment);

    int8_t socket_event_handler(const Event e, Ptr<Packet> seg, uint8_t seq_n);
    int8_t add_to_tx(Ptr<Packet> seg, uint8_t& seq_n);

    uint8_t get_address() const;
    void set_address(uint8_t);
    State get_state() const;

    typedef Callback<void, uint8_t, uint8_t, Ptr<Packet>> ReceiveCallback;
    void SetReceiveCallback(OstSocket::ReceiveCallback cb);

  private:
    void init_socket();

    int8_t segment_arrival_event_socket_handler(Ptr<Packet> seg);
    int8_t full_states_handler(Ptr<Packet> seg);
    void set_state(State);

    void send_rejection(uint8_t seq_n);
    void send_syn(uint8_t seq_n);
    void send_syn_confirm(uint8_t seq_n);
    void send_confirm(uint8_t seq_n);
    int8_t send_to_physical(SegmentFlag f, uint8_t seg_n);
    void send_spw(Ptr<Packet> segment);

    void start_close_wait_timer();
    void stop_close_wait_timer();
    void dealloc();

    int8_t add_to_rx(Ptr<Packet> seg);
    void send_to_application(Ptr<Packet> packet);

    int8_t in_tx_window(uint8_t) const;
    int8_t in_rx_window(uint8_t) const;
    int8_t tx_sliding_window_have_space() const;

    std::string state_name(State);

    Ptr<OstNode> ost;
    /* data */
    Mode mode;
    State state;
    uint8_t to_address;
    uint8_t self_address;
    uint8_t self_port;

    uint8_t to_retr;
    uint8_t tx_window_bottom;
    uint8_t tx_window_top;
    uint8_t rx_window_bottom;
    uint8_t rx_window_top;
    std::vector<Ptr<Packet>> tx_buffer;
    std::vector<Ptr<Packet>> rx_buffer;
    std::vector<bool> acknowledged;
    Ptr<TimerFifo> queue;

    ReceiveCallback application_receive_callback;

    Ptr<SpWDevice> spw_layer;
};
} // namespace ns3

#endif