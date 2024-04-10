
// Include a header file from your module to test.
// #include "ns3/spw-channel.h"

// An essential include is test.h
#include "ns3/test.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/spw-channel.h"
#include "ns3/spw-device.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include "ns3/seq-ts-header.h"

// Do not put your test classes in namespace ns3.  You may find it useful
// to use the using directive to access the ns3 namespace directly
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SpWTest");

// Add a doxygen group for tests.
// If you have more than one test, this should be in only one of them.
/**
 * \defgroup spw-tests Tests for spw
 * \ingroup spw
 * \ingroup tests
 */

// This is an example TestCase.
/**
 * \ingroup spw-tests
 * Test case for feature 1
 */
class SpwTestCase1 : public TestCase
{
  public:
    SpwTestCase1();
    virtual ~SpwTestCase1();
    void DoRun() override;

  private:
    Ptr<const Packet> m_recvdPacket;
    void SendOnePacket(Ptr<SpWDevice> device, const uint8_t* buffer, uint32_t size);
    bool RxPacket(Ptr<NetDevice> dev, Ptr<const Packet> pkt, uint16_t mode, const Address& sender);
};

// Add some help text to this case to describe what it is intended to test
SpwTestCase1::SpwTestCase1()
    : TestCase("Spw test case (does nothing)")
{
}

// This destructor does nothing but we include it as a reminder that
// the test case should clean up after itself
SpwTestCase1::~SpwTestCase1()
{
}

void
SpwTestCase1::SendOnePacket(Ptr<SpWDevice> device,
                                const uint8_t* buffer,
                                uint32_t size)
{
    Ptr<Packet> p = Create<Packet>(buffer, size);
    device->Send(p, device->GetBroadcast(), 0x800);
}

bool
SpwTestCase1::RxPacket(Ptr<NetDevice> dev,
                           Ptr<const Packet> pkt,
                           uint16_t mode,
                           const Address& sender)
{

    SeqTsHeader seqTs;
    pkt->PeekHeader(seqTs);

    NS_LOG_INFO("TraceDelay: RX some bytes from "
                                    << sender
                                    << " Sequence Number: " << seqTs.GetSeq()
                                    << " Uid: " << pkt->GetUid() << " TXtime: "
                                    << seqTs.GetTs() << " RXtime: " << Simulator::Now()
                                    << " Delay: " << Simulator::Now() - seqTs.GetTs());

    m_recvdPacket = pkt;
    return true;
}

//
// This method is the pure virtual method from class TestCase that every
// TestCase must implement
//
void
SpwTestCase1::DoRun()
{
    Ptr<Node> a = CreateObject<Node>();
    Ptr<Node> b = CreateObject<Node>();
    Ptr<SpWDevice> devA = CreateObject<SpWDevice>();
    Ptr<SpWDevice> devB = CreateObject<SpWDevice>();
    Ptr<SpWChannel> channel = CreateObject<SpWChannel>();

    devA->SetDataRate(DataRate("200Mbps"));

    devA->Attach(channel);
    devA->SetAddress(Mac48Address::Allocate());
    devA->SetQueue(CreateObject<DropTailQueue<Packet>>());
    devB->Attach(channel);
    devB->SetAddress(Mac48Address::Allocate());
    devB->SetQueue(CreateObject<DropTailQueue<Packet>>());

    a->AddDevice(devA);
    b->AddDevice(devB);

    devB->SetReceiveCallback(MakeCallback(&SpwTestCase1::RxPacket, this));
    uint8_t txBuffer[] = "\"Can you tell me where my country lies?\" \\ said the unifaun to his "
                         "true love's eyes. \\ \"It lies with me!\" cried the Queen of Maybe \\ - "
                         "for her merchandise, he traded in his prize.";
    size_t txBufferSize = sizeof(txBuffer);


    Simulator::Schedule(Seconds(1.0),
                        &SpwTestCase1::SendOnePacket,
                        this,
                        devA,
                        txBuffer,
                        txBufferSize);

    Simulator::Run();

    Ptr<Packet> received_copy = m_recvdPacket->Copy();
    SeqTsHeader seqTs;
    received_copy->RemoveHeader(seqTs);
    std::cout << "packet size with header " << seqTs.GetSerializedSize() + txBufferSize << " bytes" << std::endl;

    NS_TEST_EXPECT_MSG_EQ(received_copy->GetSize(), txBufferSize, "trivial");

    uint8_t rxBuffer[MAX_SPW_PACKET_SZ];

    received_copy->CopyData(rxBuffer, txBufferSize);
    NS_TEST_EXPECT_MSG_EQ(memcmp(rxBuffer, txBuffer, txBufferSize), 0, "trivial");

    Simulator::Destroy();
}

// The TestSuite class names the TestSuite, identifies what type of TestSuite,
// and enables the TestCases to be run.  Typically, only the constructor for
// this class must be defined

/**
 * \ingroup spw-tests
 * TestSuite for module spw
 */
class SpwTestSuite : public TestSuite
{
  public:
    SpwTestSuite();
};

SpwTestSuite::SpwTestSuite()
    : TestSuite("spw", UNIT)
{
    // TestDuration for TestCase can be QUICK, EXTENSIVE or TAKES_FOREVER
    AddTestCase(new SpwTestCase1, TestCase::QUICK);
}

// Do not forget to allocate an instance of this TestSuite
/**
 * \ingroup spw-tests
 * Static variable for test initialization
 */
static SpwTestSuite sspwTestSuite;
