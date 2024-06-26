/*
 * Copyright (c) 2007 University of Washington
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

#ifndef SPW_CHANNEL_H
#define SPW_CHANNEL_H

#include "ns3/channel.h"
#include "ns3/data-rate.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"
#include "ns3/address.h"

#include <list>
#include <unordered_map>
#include <utility>

namespace ns3
{

class SpWDevice;
class Packet;

/**
 * \ingroup point-to-point
 * \brief Simple Point To Point Channel.
 *
 * This class represents a very simple point to point channel.  Think full
 * duplex RS-232 or RS-422 with null modem and no handshaking.  There is no
 * multi-drop capability on this channel -- there can be a maximum of two
 * point-to-point net devices connected.
 *
 * There are two "wires" in the channel.  The first device connected gets the
 * [0] wire to transmit on.  The second device gets the [1] wire.  There is a
 * state (IDLE, TRANSMITTING) associated with each wire.
 *
 * \see Attach
 * \see TransmitStart
 */
class SpWChannel : public Channel
{
  public:
    /**
     * \brief Get the TypeId
     *
     * \return The TypeId for this class
     */
    static TypeId GetTypeId();

    /**
     * \brief Create a PointToPointChannel
     *
     * By default, you get a channel that has an "infinitely" fast
     * transmission speed and zero delay.
     */
    SpWChannel();

    /**
     * \brief Attach a given netdevice to this channel
     * \param device pointer to the netdevice to attach to the channel
     */
    void Attach(Ptr<SpWDevice> device);

    /**
     * \brief Transmit a packet over this channel
     * \param p Packet to transmit
     * \param src Source PointToPointNetDevice
     * \param txTime Transmit time to apply
     * \returns true if successful (currently always true)
     */
    virtual bool TransmitStart(Ptr<const Packet> p, Ptr<SpWDevice> src, Time txTime);

    bool TransmitComplete(Ptr<const Packet> p, Ptr<SpWDevice> src, Time txTime);
    void HandlingArrivedComplete(Ptr<const Packet> p,
                                     Address src,
                                     uint8_t seq_n,
                                     bool isAck,
                                     uint32_t ch_packet_seq_n,
                                     bool isReceiption);

    /**
     * \brief Get number of devices on this channel
     * \returns number of devices on this channel
     */
    std::size_t GetNDevices() const override;

    /**
     * \brief Get PointToPointNetDevice corresponding to index i on this channel
     * \param i Index number of the device requested
     * \returns Ptr to PointToPointNetDevice requested
     */
    Ptr<SpWDevice> GetSpWDevice(std::size_t i) const;

    /**
     * \brief Get NetDevice corresponding to index i on this channel
     * \param i Index number of the device requested
     * \returns Ptr to NetDevice requested
     */
    Ptr<NetDevice> GetDevice(std::size_t i) const override;

    uint32_t GetCntPackets() const;
    uint32_t IncCntPackets();
    void NotifyError(Ptr<SpWDevice> caller);
    void NullInLink(Ptr<SpWDevice> caller);
    void FCTInLink(Ptr<SpWDevice> caller);

    void PrintTransmitted();
  protected:
    /**
     * \brief Get the delay associated with this channel
     * \returns Time delay
     */
    Time GetDelay() const;

    /**
     * \brief Check to make sure the link is initialized
     * \returns true if initialized, asserts otherwise
     */
    bool IsInitialized() const;

    /**
     * \brief Get the net-device source
     * \param i the link requested
     * \returns Ptr to PointToPointNetDevice source for the
     * specified link
     */
    Ptr<SpWDevice> GetSource(uint32_t i) const;

    /**
     * \brief Get the net-device destination
     * \param i the link requested
     * \returns Ptr to PointToPointNetDevice destination for
     * the specified link
     */
    Ptr<SpWDevice> GetDestination(uint32_t i) const;

    /**
     * TracedCallback signature for packet transmission animation events.
     *
     * \param [in] packet The packet being transmitted.
     * \param [in] txDevice the TransmitTing NetDevice.
     * \param [in] rxDevice the Receiving NetDevice.
     * \param [in] duration The amount of time to transmit the packet.
     * \param [in] lastBitTime Last bit receive time (relative to now)
     * \deprecated The non-const \c Ptr<NetDevice> argument is deprecated
     * and will be changed to \c Ptr<const NetDevice> in a future release.
     */
    typedef void (*TxRxAnimationCallback)(Ptr<const Packet> packet,
                                          Ptr<NetDevice> txDevice,
                                          Ptr<NetDevice> rxDevice,
                                          Time duration,
                                          Time lastBitTime);

    void TransmissionComplete(Ptr<const Packet>, Address src, uint8_t seq_n, bool isAck, uint32_t ch_packet_seq_n, bool isReceiption); 
    void PrintTransmission(Address src, uint8_t seq_n, bool isAck, uint32_t ch_packet_seq_n, bool isReceiption, uint32_t uid) const; 


  private:
    /** Each point to point link has exactly two net devices. */
    static const std::size_t N_DEVICES = 2;
    const Time APPROACH_TIME = NanoSeconds(850); // so-called disconnect timeout window

    Time m_delay;           //!< Propagation delay
    std::size_t m_nDevices; //!< Devices of this channel

    /**
     * The trace source for the packet transmission animation events that the
     * device can fire.
     * Arguments to the callback are the packet, transmitting
     * net device, receiving net device, transmission time and
     * packet receipt time.
     *
     * \see class CallBackTraceSource
     * \deprecated The non-const \c Ptr<NetDevice> argument is deprecated
     * and will be changed to \c Ptr<const NetDevice> in a future release.
     */
    TracedCallback<Ptr<const Packet>, // Packet being transmitted
                   Ptr<NetDevice>,    // Transmitting NetDevice
                   Ptr<NetDevice>,    // Receiving NetDevice
                   Time,              // Amount of time to transmit the pkt
                   Time               // Last bit receive time (relative to now)
                   >
        m_txrxPointToPoint;

    /** \brief Wire states
     *
     */
    enum WireState
    {
        /** Initializing state */
        INITIALIZING,
        /** Idle state (no transmission from NetDevice) */
        IDLE,
        /** Transmitting state (data being transmitted from NetDevice. */
        TRANSMITTING,
        /** Propagating state (data is being propagated in the channel. */
        PROPAGATING
    };

    /**
     * \brief Wire model for the PointToPointChannel
     */
    class Link
    {
      public:
        /** \brief Create the link, it will be in INITIALIZING state
         *
         */
        Link()
            : m_state(INITIALIZING),
              m_src(nullptr),
              m_dst(nullptr)
        {
        }

        WireState m_state;                //!< State of the link
        Ptr<SpWDevice> m_src; //!< First NetDevice
        Ptr<SpWDevice> m_dst; //!< Second NetDevice
    };

    Link m_link[N_DEVICES]; //!< Link model
    uint32_t m_cnt_packets;
    std::unordered_map<uint32_t, EventId> m_events[2];
    size_t transmited[2];
    size_t packets[2];
};

} // namespace ns3

#endif /* SPW_CHANNEL_H */
