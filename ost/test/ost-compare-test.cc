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
#include <deque>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("OstCompareTest");

const char *file = "/home/danandla/BOTAY/space/develop/NS3OST/payloads/65mb";

bool ReadFileToVector(const char *const fname, std::vector<char *> &message, size_t &total_sz)
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
    return true;
}

bool ReadFileToBuffer(const char *const fname, std::vector<uint8_t> &msg_buffer, uint64_t &total_sz)
{
    std::ifstream inputFile(fname);
    if (!inputFile.is_open())
    {
        std::cout << "Error opening file: " << fname << std::endl;
        return false;
    }
    size_t sz = 0;
    uint64_t offset = 0;
    std::string line;
    while (getline(inputFile, line))
    {
        for(char ch: line)
        {
            msg_buffer.push_back(ch);
            offset++;
            sz++;
        }
        msg_buffer.push_back('\n');
        offset++;
        sz++;
    }
    inputFile.close();
    msg_buffer.push_back('\0');
    total_sz = sz;
    return true;
}

bool VerifyMsg(std::vector<char *> received_msg, const char *fname)
{
    std::vector<char *> orig;
    uint64_t total_sz = 0;
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
    uint8_t msg[10*1024*1024];
    for (Ptr<Packet> i : received_msg)
    {
        i->CopyData(msg + offset, 1024 * 64);
        offset += i->GetSize();
    }
    char buff[10*1024*1024];
    std::memcpy(buff, msg, offset);
    bool r = buff[offset - 1] == '\0';
    if(r) std::cout << "last ch is nullterm\n";
    buff[offset] = '\0';
    // for(size_t i = 0; i < offset; ++i) std::cout << buff[i];
    // std::cout << buff;
    std::cout << "Received size = " << offset << "\n";
    // return VerifyMsg(rcv, fname);
    return true;
}

class OstUser : public Object
{
public:
    OstUser(Ptr<OstNode> ost_obj, std::string nm)
        : ost(ost_obj),
          q_to_send(std::deque<size_t>()),
          name(nm),
          msg_buffer(std::vector<uint8_t>(0, 0)),
          offs(0){};
    ~OstUser(){};
    Ptr<OstNode> GetOst() { return ost; }

    void SendMsg(uint8_t to_addr, const char *fname);
    void Receive(uint8_t, Ptr<Packet> p);
    void Start() { ost->start(); }
    void Shutdown() { ost->shutdown(); }
    std::vector<Ptr<Packet>> GetReceived() { return received; }

private:
    void SendPacket(Ptr<OstNode> ost, uint8_t address, size_t sz);
    void SendPacketComplete(Ptr<OstNode> ost, uint8_t to_addr);

    Ptr<OstNode> ost;
    std::vector<Ptr<Packet>> received;
    std::deque<size_t> q_to_send;
    std::string name;
    std::vector<uint8_t> msg_buffer;
    size_t offs;
    size_t msg_sz;
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

OstCompareTestCase::OstCompareTestCase()
    : TestCase("")
{
}

OstCompareTestCase::~OstCompareTestCase()
{
}

void OstUser::SendMsg(uint8_t addr, const char *fname)
{
    uint64_t size;
    if (!ReadFileToBuffer(fname, msg_buffer, size))
        return;
    msg_sz = size;
    uint32_t maxSize = 64 * 1024; // 1 << 16
    while (size)
    {
        uint32_t packetSize;
        if (size > maxSize)
            packetSize = maxSize;
        else
            packetSize = size;
        q_to_send.push_back(packetSize);
        size -= packetSize;
    }
    NS_LOG_INFO("USER[" << name << "] sends message of sz = " << std::to_string(msg_sz) << ", that is separated by the applicatoin level into " << std::to_string(q_to_send.size()) << " packets");
    size_t sz = q_to_send.front();
    q_to_send.pop_front();
    SendPacket(ost, addr, sz);
}

void OstUser::SendPacket(Ptr<OstNode> ost, uint8_t to_address, size_t sz)
{
    uint8_t buff[1024*64];
    for(size_t i = offs; i < offs + sz; ++i) buff[i - offs] = msg_buffer[i];
    ost->send_packet(to_address, buff, sz); 
    offs += sz;
    Simulator::Schedule(MicroSeconds(1), &OstUser::SendPacketComplete, this, ost, to_address);
}

void OstUser::SendPacketComplete(Ptr<OstNode> ost, uint8_t address)
{
    if(q_to_send.empty()) return ;
    size_t sz = q_to_send.front();
    q_to_send.pop_front();
    SendPacket(ost, address, sz);
}

void OstCompareTestCase::ShutdownDevices()
{
    // if (!VerifyPacketList(userB->GetReceived(), file))
    // {
    //     NS_LOG_ERROR("unmatched message at the end");
    // };
    userA->Shutdown();
    userB->Shutdown();
}

void OstUser::Receive(uint8_t sm, Ptr<Packet> p)
{
    NS_LOG_LOGIC("NODE[" << std::to_string(ost->get_address()) << "] received packet:" << p);
    received.push_back(p);
}

void OstCompareTestCase::DoRun()
{
    Ptr<Node> a = CreateObject<Node>();
    Ptr<Node> b = CreateObject<Node>();
    Ptr<SpWDevice> devA = CreateObject<SpWDevice>();
    Ptr<SpWDevice> devB = CreateObject<SpWDevice>();
    Ptr<SpWChannel> channel = CreateObject<SpWChannel>();

    devA->SetDataRate(DataRate("200Mbps"));
    devB->SetDataRate(DataRate("200Mbps"));

    devA->Attach(channel);
    devA->SetAddress(Mac8Address::Allocate());
    devA->SetQueue(CreateObject<DropTailQueue<Packet>>());
    devB->Attach(channel);
    devB->SetAddress(Mac8Address::Allocate());
    devB->SetQueue(CreateObject<DropTailQueue<Packet>>());

    a->AddDevice(devA);
    b->AddDevice(devB);


    // ---------------TEST PARAMS---------------
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    // em->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
    file = "/home/danandla/BOTAY/space/develop/NS3OST/payloads/255kb";
    // em->SetRate(0.8);
    uint16_t window = 2;

    userA = CreateObject<OstUser>(CreateObject<OstNode>(devA, 0, window), "A");
    userA->GetOst()->SetReceiveCallback(MakeCallback(&OstUser::Receive, userA));
    userB = CreateObject<OstUser>(CreateObject<OstNode>(devB, 0, window), "B");
    userB->GetOst()->SetReceiveCallback(MakeCallback(&OstUser::Receive, userB));

    userA->GetOst()->GetSpWLayer()->SetCharacterParityErrorModel(em);
    userB->GetOst()->GetSpWLayer()->SetCharacterParityErrorModel(em);
    // ---------------TEST PARAMS---------------

    Simulator::Schedule(MilliSeconds(1600), &OstUser::Start, userA);
    Simulator::Schedule(MilliSeconds(1800),
                        &OstUser::SendMsg,
                        userA,
                        1,
                        file);

    Simulator::Schedule(Seconds(2), &OstUser::Start, userB);

    Simulator::Schedule(Seconds(200000), &OstCompareTestCase::ShutdownDevices, this);

    Simulator::Run();

    Simulator::Destroy();

    channel->PrintTransmitted();
    std::cout << "userB received "<< userB->GetReceived().size() << " packets of msg\n";
}

class OstCompareTest : public TestSuite
{
public:
    OstCompareTest();
};

OstCompareTest::OstCompareTest()
    : TestSuite("ost-compare", Type::UNIT)
{
    AddTestCase(new OstCompareTestCase, Duration::QUICK);
}

static OstCompareTest ostTestSuite;
