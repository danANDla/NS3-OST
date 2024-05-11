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
                          TimeValue(Seconds(0)),
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
      m_delay(Seconds(0.)),
      m_nDevices(0),
      m_cnt_packets(0)
{
    NS_LOG_FUNCTION_NOARGS();
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

void
SpWChannel::print_transmission(Address src,
                               uint8_t seq_n,
                               bool isAck,
                               uint32_t ch_packet_seq_n,
                               bool isReceiption) const
{
    uint32_t wire = src == m_link[0].m_src->GetAddress() ? 0 : 1;

    uint8_t senderAddress[Address::MAX_SIZE];
    src.CopyTo(senderAddress);
    uint8_t sender = senderAddress[0];

    uint8_t recieverAddress[Address::MAX_SIZE];
    m_link[wire].m_dst->GetAddress().CopyTo(recieverAddress);
    uint8_t receiver = recieverAddress[0];

    if (wire == 0)
    {
        NS_LOG_INFO("         NODE["
                    << std::to_string(sender) << "] --" << std::to_string(ch_packet_seq_n)
                    << "-> <SEQ.N=" << std::to_string(seq_n) << ">" << (isAck ? "<ACK>" : "     ")
                    << " NODE[" << std::to_string(receiver) << "] "
                    << (isReceiption ? "received" : ""));
    }
    else
    {
        NS_LOG_INFO((isReceiption ? "received" : "        ")
                    << " NODE[" << std::to_string(receiver) << "] <-"
                    << std::to_string(ch_packet_seq_n) << "-- <SEQ.N=" << std::to_string(seq_n)
                    << (isAck ? "><ACK>" : ">     ") << " NODE[" << std::to_string(sender) << "]");
    }
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
    print_transmission(src->GetAddress(), seq_n, isAck, m_cnt_packets, false);

    Simulator::ScheduleWithContext(m_link[wire].m_dst->GetNode()->GetId(),
                                   txTime + m_delay,
                                   &SpWDevice::Receive,
                                   m_link[wire].m_dst,
                                   p->Copy(),
                                   m_cnt_packets);
    Simulator::Schedule(txTime + m_delay,
                        &SpWChannel::print_transmission,
                        this,
                        src->GetAddress(),
                        seq_n,
                        isAck,
                        m_cnt_packets,
                        true);

    // Call the tx anim callback on the net device
    m_txrxPointToPoint(p, src, m_link[wire].m_dst, txTime, txTime + m_delay);
    return true;
}

void
SpWChannel::NotifyError(Ptr<SpWDevice> caller)
{
    NS_LOG_FUNCTION(this << caller);

    NS_ASSERT(m_link[0].m_state != INITIALIZING);
    NS_ASSERT(m_link[1].m_state != INITIALIZING);

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

} // namespace ns3
