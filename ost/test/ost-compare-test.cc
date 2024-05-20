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
#include "ns3/error-model.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

// Do not put your test classes in namespace ns3.  You may find it useful
// to use the using directive to access the ns3 namespace directly
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("OstCompareTest");

const char *file = "/home/danandla/BOTAY/space/develop/NS3OST/payloads/small";

bool ReadFileToVector(const char *const fname, std::vector<char *> &message, uint64_t &total_sz)
{
    std::ifstream inputFile(fname);
    if (!inputFile.is_open())
    {
        std::cout << "Error opening file: " << fname << std::endl;
        return false;
    }
    message.push_back(new char[1024 * 64]);
    uint32_t seg_number = 0;
    uint32_t offset = 0;
    std::string line;
    int i = 0;
    while (getline(inputFile, line))
    {
        if (offset + line.length() + 1 > 64 * 1024)
        {
            if (seg_number >= 9)
            {
                for (uint32_t j = 0; j < seg_number; ++j)
                    delete message[j];
                return false;
            }
            *(message[seg_number] + offset) = '\0';
            message.push_back(new char[1024 * 64]);
            seg_number += 1;
            offset = 0;
        }
        std::strcpy(message[seg_number] + offset, line.c_str());
        *(message[seg_number] + offset + line.length()) = '\n';
        offset += line.length() + 1;
        total_sz += line.length() + 1;
        i++;
    }
    // Close file when done
    inputFile.close();
    *(message[seg_number] + offset) = '\0';
    // std::cout << "total_sz = " << total_sz << " bytes, " << i << " lines" << std::endl;
    return true;
}

bool ReadFileToBuffer(const char *const fname, char* buffer, uint64_t &total_sz)
{
    std::ifstream inputFile(fname);
    if (!inputFile.is_open())
    {
        std::cout << "Error opening file: " << fname << std::endl;
        return false;
    }
    uint64_t offset = 0;
    std::string line;
    int i = 0;
    while (getline(inputFile, line))
    {
        std::strcpy(buffer + offset, line.c_str());
        *(buffer + offset + line.length()) = '\n';
        offset += line.length() + 1;
        total_sz += line.length() + 1;
        i++;
    }
    inputFile.close();
    *(buffer + offset) = '\0';
    return true;
}

bool VerifyMsg(std::vector<char *> received_msg, const char *fname)
{
    std::vector<char *> orig;
    uint64_t total_sz;
    if (!ReadFileToVector(fname, orig, total_sz))
    {
        NS_LOG_ERROR("failed to read actual");
        return false;
    }
    if (received_msg.size() != orig.size())
    {
        NS_LOG_ERROR("size differ in received msg and actual");
        return false;
    }
    for (size_t j = 0; j < orig.size() - 1; ++j)
    {
        if (std::strcmp(received_msg[j], orig[j]) != 0)
        {
            NS_LOG_ERROR("bytes differ in received msg and actual");
            return false;
        }
    }
    return true;
}

bool VerifyPacketList(std::vector<Ptr<Packet>> received_msg, const char *fname)
{
    size_t offset = 0;
    uint8_t msg[1024*1024];
    for (Ptr<Packet> i : received_msg)
    {
        i->CopyData(msg + offset, 1024 * 64);
        offset += i->GetSize();
    }
    char buff[1024*1024];
    std::memcpy(buff, msg, offset);
    bool r = buff[offset - 1] == '\0';
    if(r) std::cout << "last ch is nullterm\n";
    buff[offset] = '\0';
    for(size_t i = 0; i < offset; ++i) std::cout << buff[i];
    // std::cout << buff;
    // return VerifyMsg(rcv, fname);
    std::cout << "Received size = " << offset << "\n";
    return true;
}

class OstUser : public Object
{
public:
    OstUser(Ptr<OstNode> ost_obj)
        : ost(ost_obj),
          q_to_send(CreateObject<DropTailQueue<Packet>>()){};
    ~OstUser(){};
    Ptr<OstNode> GetOst() { return ost; }

    void SendMsg(const char *fname);
    void Receive(uint8_t, Ptr<Packet> p);
    void Start() { ost->start(); }
    void Shutdown() { ost->shutdown(); }
    std::vector<Ptr<Packet>> GetReceived() { return received; }

private:
    void SendPacket(Ptr<OstNode> ost, Ptr<Packet> p);
    void SendPacketComplete(Ptr<OstNode> ost);

    Ptr<OstNode> ost;
    std::vector<Ptr<Packet>> received;
    Ptr<Queue<Packet>> q_to_send;
};

class OstCompareTestCase : public TestCase
{
    static const int MAX_TIME_SEC = 10;

public:
    OstCompareTestCase();
    virtual ~OstCompareTestCase();
    void DoRun() override;

private:
    void ShutdownDevices();

    Ptr<OstUser> userA;
    Ptr<OstUser> userB;
};

// Add some help text to this case to describe what it is intended to test
OstCompareTestCase::OstCompareTestCase()
    : TestCase("Ost test case (does nothing)")
{
}

// This destructor does nothing but we include it as a reminder that
// the test case should clean up after itself
OstCompareTestCase::~OstCompareTestCase()
{
}

void OstUser::SendMsg(const char *fname)
{
    uint64_t size;
    char b [1024*1024];
    if (!ReadFileToBuffer(fname, b, size))
        return;

    uint8_t buffer[1024*1024];
    std::memcpy(buffer, b, size);

    uint32_t maxSize = 64 * 1024; // 1 << 16
    uint32_t offset = 0;
    NS_LOG_INFO("msgSize = " << std::to_string(size) << ", maxPAcketSz="<< std::to_string(maxSize));
    while (size)
    {
        uint32_t packetSize;
        if (size > maxSize)
            packetSize = maxSize;
        else
            packetSize = size;
        Ptr<Packet> p = Create<Packet>(buffer + offset, packetSize);
        q_to_send->Enqueue(p);
        offset += packetSize;
        size -= packetSize;
    }
    NS_LOG_INFO("sep to " << std::to_string(q_to_send->GetCurrentSize().GetValue()) << "packets");
    Ptr<Packet> p = q_to_send->Dequeue();
    SendPacket(ost, p);
}

void OstUser::SendPacket(Ptr<OstNode> ost, Ptr<Packet> p)
{
    ost->add_packet_to_transmit_fifo(p); 
    Simulator::Schedule(MicroSeconds(1), &OstUser::SendPacketComplete, this, ost);
}

void OstUser::SendPacketComplete(Ptr<OstNode> ost)
{
    Ptr<Packet> p = q_to_send->Dequeue();
    if (p)
        SendPacket(ost, p);
}

void OstCompareTestCase::ShutdownDevices()
{
    if (!VerifyPacketList(userB->GetReceived(), file))
    {
        NS_LOG_ERROR("unmatched message at the end");
    };
    userA->Shutdown();
    userB->Shutdown();
}

void OstUser::Receive(uint8_t sm, Ptr<Packet> p)
{
    NS_LOG_LOGIC("NODE[" << std::to_string(ost->get_address()) << "] received packet:" << p);
    received.push_back(p->Copy());
}

void OstCompareTestCase::DoRun()
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

    userA = CreateObject<OstUser>(CreateObject<OstNode>(devA, 0));
    userA->GetOst()->SetReceiveCallback(MakeCallback(&OstUser::Receive, userA));

    userB = CreateObject<OstUser>(CreateObject<OstNode>(devB, 0));
    userB->GetOst()->SetReceiveCallback(MakeCallback(&OstUser::Receive, userB));

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
    em->SetRate(0.3);
    userA->GetOst()->GetSpWLayer()->SetCharacterParityErrorModel(em);
    userB->GetOst()->GetSpWLayer()->SetCharacterParityErrorModel(em);

    Simulator::Schedule(MilliSeconds(1600), &OstUser::Start, userA);
    Simulator::Schedule(MilliSeconds(1800),
                        &OstUser::SendMsg,
                        userA,
                        file);

    Simulator::Schedule(Seconds(2), &OstUser::Start, userB);

    Simulator::Schedule(Seconds(20), &OstCompareTestCase::ShutdownDevices, this);

    Simulator::Run();

    Simulator::Destroy();
}

/**
 * \ingroup ost-tests
 * TestSuite for module ost
 */
class OstCompareTest : public TestSuite
{
public:
    OstCompareTest();
};

OstCompareTest::OstCompareTest()
    : TestSuite("ost-compare", Type::UNIT)
{
    // TestDuration for TestCase can be QUICK, EXTENSIVE or TAKES_FOREVER
    AddTestCase(new OstCompareTestCase, Duration::QUICK);
}

// Do not forget to allocate an instance of this TestSuite
/**
 * \ingroup ost-tests
 * Static variable for test initialization
 */
static OstCompareTest ostTestSuite;
