
// Include a header file from your module to test.

// An essential include is test.h
#include "ns3/drop-tail-queue.h"
#include "ns3/log.h"
#include "ns3/node-container.h"
#include "ns3/ost_node.h"
#include "ns3/seq-ts-header.h"
#include "ns3/simulator.h"
#include "ns3/queue.h"
#include "ns3/spw-channel.h"
#include "ns3/spw-device.h"
#include "ns3/test.h"

// Do not put your test classes in namespace ns3.  You may find it useful
// to use the using directive to access the ns3 namespace directly
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("OstTest");

// Add a doxygen group for tests.
// If you have more than one test, this should be in only one of them.
/**
 * \defgroup ost-tests Tests for ost
 * \ingroup ost
 * \ingroup tests
 */

// This is an example TestCase.
/**
 * \ingroup ost-tests
 * Test case for feature 1
 */
class OstTestCase1 : public TestCase
{
    static const int MAX_TIME_SEC = 10;

  public:
    OstTestCase1();
    virtual ~OstTestCase1();
    void DoRun() override;
    void Receive(uint8_t node_id, Ptr<Packet> p);
    std::vector<std::pair<Address, Ptr<OstNode>>> osts;
    Ptr<OstNode> GetOstByAddr(Address addr);

  private:
    void SendMsg(Ptr<OstNode> device, const uint8_t* buffer, uint32_t size);
    void SendPacket(Ptr<OstNode> ost, Ptr<Packet> p);
    void SendPacketComplete(Ptr<OstNode> ost);
    void ShutdownDevices();
    Ptr<Queue<Packet>> m_q;
};

// Add some help text to this case to describe what it is intended to test
OstTestCase1::OstTestCase1()
    : TestCase("Ost test case (does nothing)")
{
}

// This destructor does nothing but we include it as a reminder that
// the test case should clean up after itself
OstTestCase1::~OstTestCase1()
{
}

void
OstTestCase1::SendMsg(Ptr<OstNode> ost, const uint8_t* buffer, uint32_t size)
{
    m_q = CreateObject<DropTailQueue<Packet>>();
    uint32_t maxSize = 70; // 1 << 16
    uint32_t offset = 0;
    std::cout <<  "msgSize=" << size << ", maxPacketSz=" << maxSize << std::endl;
    while (size)
    {
        uint32_t packetSize;
        if(size > maxSize) 
            packetSize = maxSize;
        else
            packetSize = size;
        Ptr<Packet> p = Create<Packet>(buffer + offset, packetSize);
        m_q->Enqueue(p);
        offset += packetSize;
        size -= packetSize;
    }
    Ptr<Packet> p = m_q->Dequeue();

    SendPacket(ost, p);
}

void
OstTestCase1::SendPacket(Ptr<OstNode> ost, Ptr<Packet> p)
{
    if (ost->add_packet_to_tx(p) != 0)
    {
        NS_LOG_ERROR("Sliding window doesn't have space");
        return;
    }
    Simulator::ScheduleNow(&OstNode::event_handler, ost, APPLICATION_PACKET_READY);
    Simulator::Schedule(MicroSeconds(1), &OstTestCase1::SendPacketComplete, this, ost);
}

void
OstTestCase1::SendPacketComplete(Ptr<OstNode> ost)
{
    Ptr<Packet> p = m_q->Dequeue();
    if(p) SendPacket(ost, p);
}

void OstTestCase1::ShutdownDevices()
{
    osts[0].second->shutdown();
    osts[1].second->shutdown();
}

Ptr<OstNode>
OstTestCase1::GetOstByAddr(Address addr)
{
    Ptr<OstNode> ost;
    for (auto i : osts)
    {
        if (i.first == addr)
            ost = i.second;
    }
    return ost;
}

void
OstTestCase1::Receive(uint8_t node_id, Ptr<Packet> p)
{
    NS_LOG_LOGIC("NODE[" << std::to_string(node_id) << "] received packet:" << p);
};

void
OstTestCase1::DoRun()
{
    Ptr<Node> a = CreateObject<Node>();
    Ptr<Node> b = CreateObject<Node>();
    Ptr<SpWDevice> devA = CreateObject<SpWDevice>();
    Ptr<SpWDevice> devB = CreateObject<SpWDevice>();
    Ptr<SpWChannel> channel = CreateObject<SpWChannel>();

    devA->SetDataRate(DataRate("10Mbps"));
    devB->SetDataRate(DataRate("10Mbps"));

    devA->Attach(channel);
    devA->SetAddress(Mac8Address::Allocate());
    devA->SetQueue(CreateObject<DropTailQueue<Packet>>());
    devB->Attach(channel);
    devB->SetAddress(Mac8Address::Allocate());
    devB->SetQueue(CreateObject<DropTailQueue<Packet>>());

    a->AddDevice(devA);
    b->AddDevice(devB);

    uint8_t txBuffer[] = "\"Can you tell me where my country lies?\" \\ said the unifaun to his "
                         "true love's eyes. \\ \"It lies with me!\" cried the Queen of Maybe \\ - "
                         "for her merchandise, he traded in his prize.";
    size_t txBufferSize = sizeof(txBuffer);

    Ptr<OstNode> ostA = CreateObject<OstNode>(1, devA);
    ostA->SetReceiveCallback(MakeCallback(&OstTestCase1::Receive, this));
    Ptr<OstNode> ostB = CreateObject<OstNode>(2, devB);
    ostB->SetReceiveCallback(MakeCallback(&OstTestCase1::Receive, this));

    osts.push_back(std::make_pair(devA->GetAddress(), ostA));
    osts.push_back(std::make_pair(devB->GetAddress(), ostB));

    // Simulator::ScheduleNow(GlobalTimerTick, ostA, ostB);

    Simulator::Schedule(Seconds(1), &OstNode::start, ostA);
    Simulator::Schedule(Seconds(1),
                        &OstTestCase1::SendMsg,
                        this,
                        ostA,
                        txBuffer,
                        txBufferSize);

    Simulator::Schedule(Seconds(2), &OstNode::start, ostB);

    Simulator::Schedule(Seconds(20), &OstTestCase1::ShutdownDevices, this);

    Simulator::Run();

    Simulator::Destroy();
}

/**
 * \ingroup ost-tests
 * TestSuite for module ost
 */
class OstTestSuite : public TestSuite
{
  public:
    OstTestSuite();
};

OstTestSuite::OstTestSuite()
    : TestSuite("ost", UNIT)
{
    // TestDuration for TestCase can be QUICK, EXTENSIVE or TAKES_FOREVER
    AddTestCase(new OstTestCase1, TestCase::QUICK);
}

// Do not forget to allocate an instance of this TestSuite
/**
 * \ingroup ost-tests
 * Static variable for test initialization
 */
static OstTestSuite sostTestSuite;
