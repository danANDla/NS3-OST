/*
 * Copyright (c) 2007, 2008 University of Washington
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "spw-channel.h"

#include "spw-device.h"

#include "ns3/log.h"
#include "ns3/ost-header.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/trace-source-accessor.h"

namespace ns3
{

    NS_LOG_COMPONENT_DEFINE("SpWChannel");

    NS_OBJECT_ENSURE_REGISTERED(SpWChannel);

    TypeId
    SpWChannel::GetTypeId()
    {
        static TypeId tid =
            TypeId("ns3::SpWChannel")
                .SetParent<Channel>()
                .SetGroupName("PointToPoint")
                .AddConstructor<SpWChannel>()
                .AddAttribute("Delay",
                              "Propagation delay through the channel",
                              TimeValue(NanoSeconds(48)),
                              MakeTimeAccessor(&SpWChannel::m_delay),
                              MakeTimeChecker())
                .AddTraceSource("TxRxPointToPoint",
                                "Trace source indicating transmission of packet "
                                "from the SpWChannel, used by the Animation "
                                "interface.",
                                MakeTraceSourceAccessor(&SpWChannel::m_txrxPointToPoint),
                                "ns3::SpWChannel::TxRxAnimationCallback");
        return tid;
    }

    //
    // By default, you get a channel that
    // has an "infitely" fast transmission speed and zero delay.
    SpWChannel::SpWChannel()
        : Channel(),
          m_delay(NanoSeconds(48)),
          m_nDevices(0),
          m_cnt_packets(0),
          m_events(std::unordered_map<uint32_t, EventId>())
    {
        NS_LOG_FUNCTION_NOARGS();
        transmited[0] = 0;
        transmited[1] = 0;
        packets[0] = 0;
        packets[1] = 0;
    }

    void
    SpWChannel::Attach(Ptr<SpWDevice> device)
    {
        NS_LOG_FUNCTION(this << device);
        NS_ASSERT_MSG(m_nDevices < N_DEVICES, "Only two devices permitted");
        NS_ASSERT(device);

        m_link[m_nDevices++].m_src = device;
        //
        // If we have both devices connected to the channel, then finish introducing
        // the two halves and set the links to IDLE.
        //
        if (m_nDevices == N_DEVICES)
        {
            m_link[0].m_dst = m_link[1].m_src;
            m_link[1].m_dst = m_link[0].m_src;
            m_link[0].m_state = IDLE;
            m_link[1].m_state = IDLE;
        }
    }

    void SpWChannel::PrintTransmission(Address src,
                                       uint8_t seq_n,
                                       bool isAck,
                                       uint32_t ch_packet_seq_n,
                                       bool isReceiption,
                                       uint32_t uid) const
    {
        uint32_t wire = src == m_link[0].m_src->GetAddress() ? 0 : 1;
        uint8_t senderAddress[Address::MAX_SIZE];
        src.CopyTo(senderAddress);
        uint8_t sender = senderAddress[0];

        uint8_t recieverAddress[Address::MAX_SIZE];
        m_link[wire].m_dst->GetAddress().CopyTo(recieverAddress);
        uint8_t receiver = recieverAddress[0];
        bool with_event = false;

        char buff[500];
        char format[100];
        
        if(wire == 0)
        {
            if(isAck)
                if(isReceiption)
                    strcpy(format, "         NODE[%2d] --(%2d)-> <SEQ.N=%3d><ACK> NODE[%2d] received");
                else
                    strcpy(format, "         NODE[%2d] --(%2d)-> <SEQ.N=%3d><ACK> NODE[%2d]         ");
            else 
                if(isReceiption)
                    strcpy(format, "         NODE[%2d] --(%2d)-> <SEQ.N=%3d>      NODE[%2d] received");
                else
                    strcpy(format, "         NODE[%2d] --(%2d)-> <SEQ.N=%3d>      NODE[%2d]         ");
            if(with_event)
            {
                std::strcat(format, " | event_id=%u");
                sprintf(buff, format, sender, ch_packet_seq_n, seq_n, receiver, uid);
            }
            else{
                sprintf(buff, format, sender, ch_packet_seq_n, seq_n, receiver);
            }
        }
        else
        {
            if(isAck)
                if(isReceiption)
                    strcpy(format, "received NODE[%2d] <-(%2d)-- <SEQ.N=%3d><ACK> NODE[%2d]         ");
                else
                    strcpy(format, "         NODE[%2d] <-(%2d)-- <SEQ.N=%3d><ACK> NODE[%2d]         ");
            else 
                if(isReceiption)
                    strcpy(format, "received NODE[%3d] <-(%2d)-- <SEQ.N=%3d>      NODE[%2d]         ");
                else
                    strcpy(format, "         NODE[%2d] <-(%2d)-- <SEQ.N=%3d>      NODE[%2d]         ");

            if(with_event)
            {
                std::strcat(format, " | event_id=%u");
                sprintf(buff, format, receiver, ch_packet_seq_n, seq_n, sender, uid);
            }
            else
            {
                sprintf(buff, format, receiver, ch_packet_seq_n, seq_n, sender);
            }
        }
        NS_LOG_INFO(buff);
    }

    void
    SpWChannel::TransmissionComplete(Ptr<const Packet> p,
                                     Address src,
                                     uint8_t seq_n,
                                     bool isAck,
                                     uint32_t ch_packet_seq_n,
                                     bool isReceiption)
    {
        uint32_t wire = src == m_link[0].m_src->GetAddress() ? 0 : 1;
        m_events[wire].erase(p->GetUid());
        transmited[wire] += p->GetSize();
        packets[wire] ++;
        Simulator::ScheduleNow(
                        &SpWChannel::HandlingArrivedComplete,
                        this,
                        p,
                        src,seq_n,isAck,ch_packet_seq_n,isReceiption);
    }

    void
    SpWChannel::HandlingArrivedComplete(Ptr<const Packet> p,
                                     Address src,
                                     uint8_t seq_n,
                                     bool isAck,
                                     uint32_t ch_packet_seq_n,
                                     bool isReceiption)
    {
        uint32_t wire = src == m_link[0].m_src->GetAddress() ? 0 : 1;
        PrintTransmission(src, seq_n, isAck, ch_packet_seq_n, isReceiption, m_events[wire][p->GetUid()].GetUid());
        Simulator::ScheduleNow(
                        &SpWDevice::Receive,
                        m_link[wire].m_dst,
                        p->Copy(),
                        m_cnt_packets);
    }

    bool
    SpWChannel::TransmitStart(Ptr<const Packet> p, Ptr<SpWDevice> src, Time txTime)
    {
        NS_LOG_FUNCTION(this << p << src);

        NS_ASSERT(m_link[0].m_state != INITIALIZING);
        NS_ASSERT(m_link[1].m_state != INITIALIZING);

        uint32_t wire = src == m_link[0].m_src ? 0 : 1;

        OstHeader h;
        p->PeekHeader(h);
        NS_LOG_FUNCTION(h);
        IncCntPackets();
        bool isAck = h.is_ack();

        uint8_t seq_n = h.get_seq_number();
        EventId event = Simulator::Schedule(txTime + m_delay,
                                            &SpWChannel::TransmissionComplete,
                                            this,
                                            p,
                                            src->GetAddress(),
                                            seq_n,
                                            isAck,
                                            m_cnt_packets,
                                            true);
        PrintTransmission(src->GetAddress(), seq_n, isAck, m_cnt_packets, false, event.GetUid());
        m_events[wire][p->GetUid()]=event;

        m_txrxPointToPoint(p, src, m_link[wire].m_dst, txTime, txTime + m_delay);
        return true;
    }

    void
    SpWChannel::NotifyError(Ptr<SpWDevice> caller)
    {
        NS_LOG_FUNCTION(this << caller);

        NS_ASSERT(m_link[0].m_state != INITIALIZING);
        NS_ASSERT(m_link[1].m_state != INITIALIZING);

        for (int w = 0; w < 2; ++w) // remove all packets from channel
        {
            for (auto& it: m_events[w]) {
                NS_LOG_LOGIC("canceling " << std::to_string(it.second.GetUid()) << " event");
                Simulator::Cancel(it.second);
            }
            m_events[w].clear();
        }

        uint32_t wire = caller == m_link[0].m_src ? 0 : 1;
        Simulator::Schedule(APPROACH_TIME, &SpWDevice::ApproachLinkDisconnection, m_link[wire].m_dst);
    }

    bool
    SpWChannel::LinkReady(Ptr<SpWDevice> caller)
    {
        NS_LOG_FUNCTION(this << caller);

        NS_ASSERT(m_link[0].m_state != INITIALIZING);
        NS_ASSERT(m_link[1].m_state != INITIALIZING);

        uint32_t wire = caller == m_link[0].m_src ? 0 : 1;
        return m_link[wire].m_dst->IsReadyToConnect();
    }

    std::size_t
    SpWChannel::GetNDevices() const
    {
        NS_LOG_FUNCTION_NOARGS();
        return m_nDevices;
    }

    Ptr<SpWDevice>
    SpWChannel::GetSpWDevice(std::size_t i) const
    {
        NS_LOG_FUNCTION_NOARGS();
        NS_ASSERT(i < 2);
        return m_link[i].m_src;
    }

    Ptr<NetDevice>
    SpWChannel::GetDevice(std::size_t i) const
    {
        NS_LOG_FUNCTION_NOARGS();
        return GetSpWDevice(i);
    }

    Time
    SpWChannel::GetDelay() const
    {
        return m_delay;
    }

    Ptr<SpWDevice>
    SpWChannel::GetSource(uint32_t i) const
    {
        return m_link[i].m_src;
    }

    Ptr<SpWDevice>
    SpWChannel::GetDestination(uint32_t i) const
    {
        return m_link[i].m_dst;
    }

    bool
    SpWChannel::IsInitialized() const
    {
        NS_ASSERT(m_link[0].m_state != INITIALIZING);
        NS_ASSERT(m_link[1].m_state != INITIALIZING);
        return true;
    }

    uint32_t
    SpWChannel::GetCntPackets() const
    {
        return m_cnt_packets;
    }

    uint32_t
    SpWChannel::IncCntPackets()
    {
        return ++m_cnt_packets;
    }

    void
    SpWChannel::PrintTransmitted() {
        std::cout << " --> " << transmited[0] << " bytes, " << packets[0] << " packets\n";
        std::cout << " <-- " << transmited[1] << " bytes, " << packets[1] << " packets\n";
        std::cout << " total " << transmited[0] + transmited[1] << " bytes, " << packets[1] + packets[0] << "packets\n";
    }
} // namespace ns3
