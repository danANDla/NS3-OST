#include "ost_node.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <stdio.h>
#include <string.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("OstNode");

OstNode:: OstNode(uint8_t id, Ptr<SpWDevice> dev)
    :   simulator_id(id),
        tx_window_bottom(0),
        tx_window_top(0),
        to_receive(0),
        tx_buffer(std::vector<Ptr<Packet>> (255)),
        rx_buffer(nullptr),
        queue(TimerFifo(WINDOW_SZ)),
        spw_layer(dev)
{
    queue.set_callback(MakeCallback(&OstNode::hw_timer_handler, this));
    spw_layer->SetReceiveCallback(MakeCallback(&OstNode::network_layer_handler, this));
}

OstNode::~OstNode() { }

void OstNode::add_packet_to_rx(Ptr<Packet> p) {
    rx_buffer = p;
}

bool OstNode::tx_sliding_window_have_space() {
    if(tx_window_top >= tx_window_bottom) {
        return tx_window_top - tx_window_bottom < WINDOW_SZ;
    } else {
        return MAX_UNACK_PACKETS - tx_window_top + 1 + tx_window_bottom < WINDOW_SZ;
    }
}

int8_t OstNode::add_packet_to_tx(Ptr<Packet> p) {
    if(tx_sliding_window_have_space()) {
        tx_buffer[tx_window_top] = p;
        tx_window_top = (tx_window_top + 1)  % MAX_UNACK_PACKETS;
        return 0;
    }
    NS_LOG_ERROR("sliding window run out of space");
    return 1;
}

void OstNode::send_to_application(Ptr<Packet> packet) {
    // NS_LOG_INFO("received new packet from spw layer");
}

int8_t OstNode::send_to_physical(SegmentType t, uint8_t seq_n) {
    OstHeader header;
    if(t == DATA) {
        Ptr<Packet> p = tx_buffer[seq_n]->Copy();
        header.set_payload_len(p->GetSize());
        header.set_seq_number(seq_n);
        header.set_type(DATA);
        header.set_src_addr(-1);
        p->AddHeader(header);
        spw_layer->Send(p, spw_layer->GetBroadcast(), 0);
        queue.add_new_timer(tx_window_bottom, 100);
    } else if (t == ACK) {
        Ptr<Packet> p = Create<Packet>();
        header.set_payload_len(0);
        header.set_seq_number(seq_n);
        header.set_type(ACK);
        header.set_src_addr(-1);
        p->AddHeader(header);
        spw_layer->Send(p, spw_layer->GetBroadcast(), 0);
    }
    NS_LOG_INFO("NODE[" << std::to_string(simulator_id) << "] Sent packet " << segment_type_name(t) << " to spw layer, " << header);
    return 0;
}

bool OstNode::in_tx_window(uint8_t seq_n) {

}

bool OstNode::in_rx_window(uint8_t seq_n) {

}

int8_t OstNode::transport_layer_receive() {
    OstHeader header; 
    rx_buffer->RemoveHeader(header);

    if(header.is_ack()) {
        if(in_tx_window(header.get_seq_number())) {
            queue.cancel_timer(tx_window_bottom);
            tx_window_bottom = (tx_window_bottom + 1) % MAX_UNACK_PACKETS;
        } 
    } else {
        if(in_rx_window(header.get_seq_number()) == to_receive) {
            send_to_application(rx_buffer);
        }
        send_to_physical(ACK, to_receive);
    }
    return 0;
}

int8_t OstNode::get_packet_from_application() {
    uint8_t* buff = new uint8_t[200];
    uint8_t t = 1;
    buff[0] = 1;
    for(int i = 0; i < 129; ++i) {
        buff[i] = t * (t + 1);
        t += buff[i];
    }
    // tx_buffer[tx_window_bottom] = Packet(buff, 129);    
    return 0;
}

int8_t OstNode::event_handler(const TransportLayerEvent e) {
    if(e != TRANSPORT_CLK_INTERRUPT)
        NS_LOG_INFO("NODE[" << std::to_string(simulator_id) << "] event: " << event_name(e));

    switch (e)
    {
    case PACKET_ARRIVED_FROM_NETWORK:
        transport_layer_receive();
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

bool OstNode::hw_timer_handler(uint8_t seq_n) {
    to_retr = seq_n;
    event_handler(RETRANSMISSION_INTERRUPT);
    return true;
}

bool OstNode::network_layer_handler(Ptr<NetDevice> dev,
                           Ptr<const Packet> pkt,
                           uint16_t mode,
                           const Address& sender) {
    add_packet_to_rx(pkt->Copy());
    Simulator::ScheduleNow(&OstNode::event_handler, this, PACKET_ARRIVED_FROM_NETWORK);
    return true;
}

std::string OstNode::segment_type_name(SegmentType t) {
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

std::string OstNode::event_name(TransportLayerEvent e) {
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
   case CHECKSUM_ERR:
    return "CHECKSUM_ERR";
   default:
    return "unknown event";
   }  
}
}
