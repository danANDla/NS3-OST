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

#include "spw-device.h"

#include "spw-channel.h"

#include "ns3/error-model.h"
#include "ns3/llc-snap-header.h"
#include "ns3/log.h"
#include "ns3/mac8-address.h"
#include "ns3/ost-header.h"
#include "ns3/pointer.h"
#include "ns3/queue.h"
#include "ns3/simulator.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/uinteger.h"


namespace ns3
{

NS_LOG_COMPONENT_DEFINE("SpWDevice");

NS_OBJECT_ENSURE_REGISTERED(SpWDevice);

TypeId
SpWDevice::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::SpWDevice")
            .SetParent<NetDevice>()
            .SetGroupName("SpW")
            .AddConstructor<SpWDevice>()
            .AddAttribute("Mtu",
                          "The MAC-level Maximum Transmission Unit",
                          UintegerValue(DEFAULT_MTU),
                          MakeUintegerAccessor(&SpWDevice::SetMtu, &SpWDevice::GetMtu),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("DataRate",
                          "The default data rate for point to point links",
                          DataRateValue(DataRate("32768b/s")),
                          MakeDataRateAccessor(&SpWDevice::m_bps),
                          MakeDataRateChecker())
            .AddAttribute("ReceiveErrorModel",
                          "The receiver error model used to simulate spw character parity errors",
                          PointerValue(),
                          MakePointerAccessor(&SpWDevice::m_characterParityErrorModel),
                          MakePointerChecker<ErrorModel>())
            .AddAttribute("InterframeGap",
                          "The time to wait between packet (frame) transmissions",
                          TimeValue(Seconds(0.0)),
                          MakeTimeAccessor(&SpWDevice::m_tInterframeGap),
                          MakeTimeChecker())

            //
            // Transmit queueing discipline for the device which includes its own set
            // of trace hooks.
            //
            .AddAttribute("TxQueue",
                          "A queue to use as the transmit queue in the device.",
                          PointerValue(),
                          MakePointerAccessor(&SpWDevice::m_queue),
                          MakePointerChecker<Queue<Packet>>())

            //
            // Trace sources at the "top" of the net device, where packets transition
            // to/from higher layers.
            //
            .AddTraceSource("MacTx",
                            "Trace source indicating a packet has arrived "
                            "for transmission by this device",
                            MakeTraceSourceAccessor(&SpWDevice::m_macTxTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("MacTxDrop",
                            "Trace source indicating a packet has been dropped "
                            "by the device before transmission",
                            MakeTraceSourceAccessor(&SpWDevice::m_macTxDropTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("MacPromiscRx",
                            "A packet has been received by this device, "
                            "has been passed up from the physical layer "
                            "and is being forwarded up the local protocol stack.  "
                            "This is a promiscuous trace,",
                            MakeTraceSourceAccessor(&SpWDevice::m_macPromiscRxTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("MacRx",
                            "A packet has been received by this device, "
                            "has been passed up from the physical layer "
                            "and is being forwarded up the local protocol stack.  "
                            "This is a non-promiscuous trace,",
                            MakeTraceSourceAccessor(&SpWDevice::m_macRxTrace),
                            "ns3::Packet::TracedCallback")
#if 0
    // Not currently implemented for this device
    .AddTraceSource ("MacRxDrop",
                     "Trace source indicating a packet was dropped "
                     "before being forwarded up the stack",
                     MakeTraceSourceAccessor (&PointToPointNetDevice::m_macRxDropTrace),
                     "ns3::Packet::TracedCallback")
#endif
            //
            // Trace sources at the "bottom" of the net device, where packets transition
            // to/from the channel.
            /// Se
            .AddTraceSource("PhyTxBegin",
                            "Trace source indicating a packet has begun "
                            "transmitting over the channel",
                            MakeTraceSourceAccessor(&SpWDevice::m_phyTxBeginTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("PhyTxEnd",
                            "Trace source indicating a packet has been "
                            "completely transmitted over the channel",
                            MakeTraceSourceAccessor(&SpWDevice::m_phyTxEndTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("PhyTxDrop",
                            "Trace source indicating a packet has been "
                            "dropped by the device during transmission",
                            MakeTraceSourceAccessor(&SpWDevice::m_phyTxDropTrace),
                            "ns3::Packet::TracedCallback")
#if 0
    // Not currently implemented for this device
    .AddTraceSource ("PhyRxBegin",
                     "Trace source indicating a packet has begun "
                     "being received by the device",
                     MakeTraceSourceAccessor (&PointToPointNetDevice::m_phyRxBeginTrace),
                     "ns3::Packet::TracedCallback")
#endif
            .AddTraceSource("PhyRxEnd",
                            "Trace source indicating a packet has been "
                            "completely received by the device",
                            MakeTraceSourceAccessor(&SpWDevice::m_phyRxEndTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("PhyRxDrop",
                            "Trace source indicating a packet has been "
                            "dropped by the device during reception",
                            MakeTraceSourceAccessor(&SpWDevice::m_phyRxDropTrace),
                            "ns3::Packet::TracedCallback")

            //
            // Trace sources designed to simulate a packet sniffer facility (tcpdump).
            // Note that there is really no difference between promiscuous and
            // non-promiscuous traces in a point-to-point link.
            //
            .AddTraceSource("Sniffer",
                            "Trace source simulating a non-promiscuous packet sniffer "
                            "attached to the device",
                            MakeTraceSourceAccessor(&SpWDevice::m_snifferTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("PromiscSniffer",
                            "Trace source simulating a promiscuous packet sniffer "
                            "attached to the device",
                            MakeTraceSourceAccessor(&SpWDevice::m_promiscSnifferTrace),
                            "ns3::Packet::TracedCallback");
    return tid;
}

SpWDevice::SpWDevice()
    : m_machineState(DOWN),
      m_channel(nullptr),
      m_linkUp(false),
      m_currentPkt(nullptr),
      transmit_complete_events(std::unordered_map<uint64_t, EventId>())
{
    NS_LOG_FUNCTION(this);
}

SpWDevice::~SpWDevice()
{
    NS_LOG_FUNCTION(this);
}

void
SpWDevice::AddHeader(Ptr<Packet> p, uint16_t protocolNumber)
{
    NS_LOG_FUNCTION(this << p << protocolNumber);
    // PppHeader ppp;
    // ppp.SetProtocol(EtherToPpp(protocolNumber));
    // p->AddHeader(ppp);
}

bool
SpWDevice::ProcessHeader(Ptr<Packet> p, uint16_t& param)
{
    // NS_LOG_FUNCTION(this << p << param);
    // PppHeader ppp;
    // p->RemoveHeader(ppp);
    // param = PppToEther(ppp.GetProtocol());
    return true;
}

void
SpWDevice::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_node = nullptr;
    m_channel = nullptr;
    m_characterParityErrorModel = nullptr;
    m_currentPkt = nullptr;
    m_queue = nullptr;
    NetDevice::DoDispose();
}

void
SpWDevice::SetDataRate(DataRate bps)
{
    NS_LOG_FUNCTION(this);
    m_bps = bps;
}

void
SpWDevice::SetInterframeGap(Time t)
{
    NS_LOG_FUNCTION(this << t.As(Time::S));
    m_tInterframeGap = t;
}

bool
SpWDevice::TransmitStart(Ptr<Packet> p)
{
    NS_LOG_FUNCTION(this << p);
    NS_LOG_LOGIC("UID is " << p->GetUid() << ", size is " << p->GetSerializedSize());
    NS_ASSERT_MSG(m_machineState == RUN, "Must be RUN to transmit");

    m_machineState = BUSY;
    m_currentPkt = p;
    m_phyTxBeginTrace(m_currentPkt);

    Time txTime = m_bps.CalculateBytesTxTime(p->GetSize());
    Time txCompleteTime = txTime;

    OstHeader header;
    p->PeekHeader(header);

    EventId event = Simulator::Schedule(txCompleteTime,
                        &SpWDevice::TransmitComplete,
                        this,
                        p->GetUid());
    transmit_complete_events[p->GetUid()] = event;
    NS_LOG_LOGIC("SPW[" << std::to_string(address) <<"] start transmitting | uid " << std::to_string(event.GetUid()));

    bool result = m_channel->TransmitStart(p, this, txTime);
    if (!result)
    {
        m_phyTxDropTrace(p);
    }

    NS_LOG_LOGIC("SPW[" << std::to_string(address) << "] planned to complete transmission: " << std::to_string(transmit_complete_events.size()));
    return result;
}

void
SpWDevice::TransmitComplete(uint64_t uint) 
{
    NS_LOG_FUNCTION(this);

    //
    // This function is called to when we're all done transmitting a packet.
    // We try and pull another packet off of the transmit queue.  If the queue
    // is empty, we are done, otherwise we need to start transmitting the
    // next packet.
    //
    NS_ASSERT_MSG(m_machineState == BUSY, "SPW[" << std::to_string(address) << "] must be BUSY if transmitting, but was " << std::to_string(m_machineState));
    m_machineState = RUN;

    NS_ASSERT_MSG(m_currentPkt, "SpWDevice::TransmitComplete(): m_currentPkt zero");

    transmit_complete_events.erase(uint);


    m_phyTxEndTrace(m_currentPkt);
    m_currentPkt = nullptr;

    Ptr<Packet> p = m_queue->Dequeue();
    if (!p)
    {
        NS_LOG_LOGIC("No pending packets in device queue after tx complete");
        device_ready_cb();
        return;
    }

    //
    // Got another packet off of the queue, so start the transmit process again.
    //
    m_snifferTrace(p);
    m_promiscSnifferTrace(p);
    TransmitStart(p);
}

bool
SpWDevice::Attach(Ptr<SpWChannel> ch)
{
    NS_LOG_FUNCTION(this << &ch);

    m_channel = ch;

    m_channel->Attach(this);

    //
    // This device is up whenever it is attached to a channel.  A better plan
    // would be to have the link come up when both devices are attached, but this
    // is not done for now.
    //
    NotifyLinkUp();
    return true;
}

void
SpWDevice::SetQueue(Ptr<Queue<Packet>> q)
{
    NS_LOG_FUNCTION(this << q);
    m_queue = q;
}

void
SpWDevice::SetCharacterParityErrorModel(Ptr<ErrorModel> em)
{
    NS_LOG_FUNCTION(this << em);
    m_characterParityErrorModel = em;
}

void
SpWDevice::Receive(Ptr<Packet> packet, uint32_t ch_packet_seq_n)
{
    NS_LOG_FUNCTION(this << packet);
    uint16_t protocol = 0;

    if (m_characterParityErrorModel && m_characterParityErrorModel->IsCorrupt(packet))
    {
        m_phyRxDropTrace(packet);
        NS_LOG_INFO("SPW[" << std::to_string(address) << "] detected error. Reconnecting. planned to complete transmission: " << std::to_string(transmit_complete_events.size()));
        m_channel->NotifyError(this);
        ErrorResetSpWState();
    }
    else
    {
        //
        // Hit the trace hooks.  All of these hooks are in the same place in this
        // device because it is so simple, but this is not usually the case in
        // more complicated devices.
        //
        m_snifferTrace(packet);
        m_promiscSnifferTrace(packet);
        m_phyRxEndTrace(packet);

        //
        // Trace sinks will expect complete packets, not packets without some of the
        // headers.
        //
        Ptr<Packet> originalPacket = packet->Copy();

        //
        // Strip off the point-to-point protocol header and forward this packet
        // up the protocol stack.  Since this is a simple point-to-point link,
        // there is no difference in what the promisc callback sees and what the
        // normal receive callback sees.
        //
        // ProcessHeader(packet, protocol);

        if (!m_promiscCallback.IsNull())
        {
            m_macPromiscRxTrace(originalPacket);
            m_promiscCallback(this,
                              packet,
                              protocol,
                              GetRemote(),
                              GetAddress(),
                              NetDevice::PACKET_HOST);
        }

        m_macRxTrace(originalPacket);
        m_rxCallback(this, packet, protocol, GetRemote());
    }
}

Ptr<Queue<Packet>>
SpWDevice::GetQueue() const
{
    NS_LOG_FUNCTION(this);
    return m_queue;
}

void
SpWDevice::NotifyLinkUp()
{
    NS_LOG_FUNCTION(this);
    m_linkUp = true;
    m_linkChangeCallbacks();
}

void
SpWDevice::NotifyLinksDown()
{
    NS_LOG_FUNCTION(this);
    m_linkUp = false;
    m_linkChangeCallbacks();
}

void
SpWDevice::SetIfIndex(const uint32_t index)
{
    NS_LOG_FUNCTION(this);
    m_ifIndex = index;
}

uint32_t
SpWDevice::GetIfIndex() const
{
    return m_ifIndex;
}

Ptr<Channel>
SpWDevice::GetChannel() const
{
    return m_channel;
}

Ptr<SpWChannel>
SpWDevice::GetSpWChannel() const
{
    return m_channel;
}

//
// This is a point-to-point device, so we really don't need any kind of address
// information.  However, the base class NetDevice wants us to define the
// methods to get and set the address.  Rather than be rude and assert, we let
// clients get and set the address, but simply ignore them.

void
SpWDevice::SetAddress(Address addr)
{
    NS_LOG_FUNCTION(this << addr);
    m_address = Mac8Address::ConvertFrom(addr);
    m_address.CopyTo(&address);
}

Address
SpWDevice::GetAddress() const
{
    return m_address;
}

bool
SpWDevice::IsLinkUp() const
{
    NS_LOG_FUNCTION(this);
    return m_linkUp;
}

void
SpWDevice::AddLinkChangeCallback(Callback<void> callback)
{
    NS_LOG_FUNCTION(this);
    m_linkChangeCallbacks.ConnectWithoutContext(callback);
}

//
// This is a point-to-point device, so every transmission is a broadcast to
// all of the devices on the network.
//
bool
SpWDevice::IsBroadcast() const
{
    NS_LOG_FUNCTION(this);
    return true;
}

//
// We don't really need any addressing information since this is a
// point-to-point device.  The base class NetDevice wants us to return a
// broadcast address, so we make up something reasonable.
//
Address
SpWDevice::GetBroadcast() const
{
    NS_LOG_FUNCTION(this);
    return Mac48Address("ff:ff:ff:ff:ff:ff");
}

bool
SpWDevice::IsMulticast() const
{
    NS_LOG_FUNCTION(this);
    return true;
}

Address
SpWDevice::GetMulticast(Ipv4Address multicastGroup) const
{
    NS_LOG_FUNCTION(this);
    return Mac48Address("01:00:5e:00:00:00");
}

Address
SpWDevice::GetMulticast(Ipv6Address addr) const
{
    NS_LOG_FUNCTION(this << addr);
    return Mac48Address("33:33:00:00:00:00");
}

bool
SpWDevice::IsPointToPoint() const
{
    NS_LOG_FUNCTION(this);
    return true;
}

bool
SpWDevice::IsBridge() const
{
    NS_LOG_FUNCTION(this);
    return false;
}

bool
SpWDevice::Send(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
    // If IsLinkUp() is false it means there is no channel to send any packet
    // over so we just hit the drop trace on the packet and return an error.
    //
    if (!IsLinkUp())
    {
        m_macTxDropTrace(packet);
        return false;
    }

    // SeqTsHeader seqTs;
    // seqTs.SetSeq(0);

    m_macTxTrace(packet);

    // packet->AddHeader(seqTs);

    //
    // We should enqueue and dequeue the packet to hit the tracing hooks.
    //
    if (m_queue->Enqueue(packet))
    {
        //
        // If the channel is ready for transition we send the packet right now
        //
        if (m_machineState == RUN)
        {
            packet = m_queue->Dequeue();
            m_snifferTrace(packet);
            m_promiscSnifferTrace(packet);
            bool ret = TransmitStart(packet);
            return ret;
        }
        return true;
    }

    // Enqueue may fail (overflow)

    m_macTxDropTrace(packet);
    return false;
}

void
SpWDevice::CheckQueue()
{
    if (m_machineState == DOWN) {
        // m_queue->Flush();
        return;
    }
    if (m_machineState == RUN && !m_queue->IsEmpty())
    {
        Ptr<Packet> packet = m_queue->Dequeue();
        m_snifferTrace(packet);
        m_promiscSnifferTrace(packet);
        TransmitStart(packet);
    }
    return;
}

void
SpWDevice::ErrorResetSpWState()
{
    NS_LOG_LOGIC("SPW[" << std::to_string(address) <<"] ERROR_RESET");
    m_machineState = ERROR_RESET;
    for (auto& it: transmit_complete_events) {
        NS_LOG_LOGIC("SPW[" << std::to_string(address) <<"] canceling receive | uid " << std::to_string(it.second.GetUid()));
        Simulator::Cancel(it.second);
    }
    transmit_complete_events.clear();
    Simulator::Cancel(sendNull);
    Simulator::Cancel(sendFCT);

    Simulator::Schedule(SPW_HALF_DELAY, &SpWDevice::ErrorWaitSpWState, this);
}

void
SpWDevice::ErrorWaitSpWState()
{
    NS_LOG_LOGIC("SPW[" << std::to_string(address) <<"] ERROR_WAIT");
    m_machineState = ERROR_WAIT;
    Simulator::Schedule(SPW_DELAY, &SpWDevice::ReadySpWState, this);
}

void
SpWDevice::ReadySpWState()
{
    NS_LOG_LOGIC("SPW[" << std::to_string(address) <<"] READY");
    m_machineState = READY;
    StartedSpWState(); // auto-start
}

void
SpWDevice::StartedSpWState()
{
    NS_LOG_LOGIC("SPW[" << std::to_string(address) <<"] STARTED");
    m_machineState = STARTED;
    Simulator::Cancel(stateChangeToErrorResetEventId);
    stateChangeToErrorResetEventId = Simulator::Schedule(SPW_DELAY, &SpWDevice::ErrorResetSpWState, this);
    sendNull = Simulator::ScheduleNow(&SpWDevice::SendNull, this);
}

void
SpWDevice::ConnectingSpWState()
{
    NS_LOG_LOGIC("SPW[" << std::to_string(address) <<"] CONNECTING");
    m_machineState = CONNECTING;
    Simulator::Cancel(stateChangeToErrorResetEventId);
    stateChangeToErrorResetEventId = Simulator::Schedule(SPW_DELAY, &SpWDevice::ErrorResetSpWState, this);
    sendFCT = Simulator::ScheduleNow(&SpWDevice::SendFCT, this);
}

void
SpWDevice::RunSpWState()
{
    NS_LOG_LOGIC("SPW[" << std::to_string(address) <<"] RUN");
    m_machineState = RUN;
    Simulator::Cancel(stateChangeToErrorResetEventId);
    Simulator::Cancel(sendFCT);
    Simulator::Cancel(sendNull);

    uint8_t addr;
    m_address.CopyTo(&addr);
    NS_LOG_INFO("SPW[" <<std::to_string(addr) << "] CONNECTED!");
    device_ready_cb();
    CheckQueue();
}


void
SpWDevice::Shutdown()
{
    m_machineState = DOWN;
}

void SpWDevice::SendNull()
{
    m_channel->NullInLink(this);
    if (m_machineState != ERROR_RESET) {
        sendNull = Simulator::Schedule(NanoSeconds(50), &SpWDevice::SendNull, this);
    }
}

void SpWDevice::ReceiveNull()
{
    if(m_machineState == STARTED) {
        Simulator::Cancel(sendNull);
        Simulator::Cancel(stateChangeToErrorResetEventId);
        stateChangeToErrorResetEventId = Simulator::Schedule(SPW_DELAY, &SpWDevice::ErrorResetSpWState, this);
        ConnectingSpWState();
    }
    else if (m_machineState == READY) {
        StartedSpWState();
    }
}

void SpWDevice::SendFCT()
{
    m_channel->FCTInLink(this);
    if (m_machineState != ERROR_RESET) {
        sendFCT = Simulator::Schedule(NanoSeconds(50), &SpWDevice::SendFCT, this);
    }
}

void SpWDevice::ReceiveFCT()
{
    if(m_machineState == CONNECTING) {
        Simulator::Cancel(sendFCT);
        Simulator::Cancel(stateChangeToErrorResetEventId);
        RunSpWState();
    }
}

void
SpWDevice::ApproachLinkDisconnection()
{
    NS_LOG_INFO("SPW[" << std::to_string(address) << "] approach link disconnected, planned to complete transmission: " << std::to_string(transmit_complete_events.size()));
    for (auto& it: transmit_complete_events) {
        NS_LOG_LOGIC("SPW[" << std::to_string(address) <<"] canceling completion of transmit | uid " << std::to_string(it.second.GetUid()));
        Simulator::Cancel(it.second);
    }
    transmit_complete_events.clear();
    Simulator::Cancel(sendFCT);
    Simulator::Cancel(sendNull);

    m_machineState = ERROR_RESET;
    Simulator::Cancel(stateChangeToErrorResetEventId);
    stateChangeToErrorResetEventId = Simulator::Schedule(EXCHANGE_OF_SILENCE, &SpWDevice::ErrorResetSpWState, this);
}

bool
SpWDevice::IsStarted() {
    return m_machineState == STARTED;
}

bool
SpWDevice::IsConnecting() {
    return m_machineState == CONNECTING;
}

bool
SpWDevice::IsReadyToTransmit() {
    return m_machineState == RUN;
}


bool
SpWDevice::SendFrom(Ptr<Packet> packet,
                    const Address& source,
                    const Address& dest,
                    uint16_t protocolNumber)
{
    NS_LOG_FUNCTION(this << packet << source << dest << protocolNumber);
    return false;
}

Ptr<Node>
SpWDevice::GetNode() const
{
    return m_node;
}

void
SpWDevice::SetNode(Ptr<Node> node)
{
    NS_LOG_FUNCTION(this);
    m_node = node;
}

bool
SpWDevice::NeedsArp() const
{
    NS_LOG_FUNCTION(this);
    return false;
}

void
SpWDevice::SetReceiveCallback(NetDevice::ReceiveCallback cb)
{
    m_rxCallback = cb;
}

void
SpWDevice::SetPromiscReceiveCallback(NetDevice::PromiscReceiveCallback cb)
{
    m_promiscCallback = cb;
}

void
SpWDevice::SetPacketSentCallcback(PacketSentCallback cb)
{
    packet_sent_cb = cb;
}

void
SpWDevice::SetDeviceReadyCallback(DeviceReadyCallback cb)
{
    device_ready_cb = cb;
}

bool
SpWDevice::SupportsSendFrom() const
{
    NS_LOG_FUNCTION(this);
    return false;
}

Address
SpWDevice::GetRemote() const
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT(m_channel->GetNDevices() == 2);
    for (std::size_t i = 0; i < m_channel->GetNDevices(); ++i)
    {
        Ptr<NetDevice> tmp = m_channel->GetDevice(i);
        if (tmp != this)
        {
            return tmp->GetAddress();
        }
    }
    NS_ASSERT(false);
    // quiet compiler.
    return Address();
}

bool
SpWDevice::SetMtu(uint16_t mtu)
{
    NS_LOG_FUNCTION(this << mtu);
    m_mtu = mtu;
    return true;
}

uint16_t
SpWDevice::GetMtu() const
{
    NS_LOG_FUNCTION(this);
    return m_mtu;
}

uint16_t
SpWDevice::PppToEther(uint16_t proto)
{
    NS_LOG_FUNCTION_NOARGS();
    switch (proto)
    {
    case 0x0021:
        return 0x0800; // IPv4
    case 0x0057:
        return 0x86DD; // IPv6
    default:
        NS_ASSERT_MSG(false, "PPP Protocol number not defined!");
    }
    return 0;
}

uint16_t
SpWDevice::EtherToPpp(uint16_t proto)
{
    NS_LOG_FUNCTION_NOARGS();
    switch (proto)
    {
    case 0x0800:
        return 0x0021; // IPv4
    case 0x86DD:
        return 0x0057; // IPv6
    default:
        NS_ASSERT_MSG(false, "PPP Protocol number not defined!");
    }
    return 0;
}

} // namespace ns3
