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
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/ost-header.h"

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
      m_nDevices(0)
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

    Simulator::ScheduleWithContext(m_link[wire].m_dst->GetNode()->GetId(),
                                   txTime + m_delay,
                                   &SpWDevice::Receive,
                                   m_link[wire].m_dst,
                                   p->Copy());

    // Call the tx anim callback on the net device
    m_txrxPointToPoint(p, src, m_link[wire].m_dst, txTime, txTime + m_delay);
    return true;
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

} // namespace ns3
