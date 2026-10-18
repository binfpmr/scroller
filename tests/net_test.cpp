#include "net/stack.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "test.h"

namespace {

using Frame = std::vector<uint8_t>;

class FakeLink : public net::Link {
 public:
  bool Send(const uint8_t* frame, size_t length) override {
    sent.emplace_back(frame, frame + length);
    return true;
  }
  Frame Take() {
    Frame f = sent.front();
    sent.pop_front();
    return f;
  }
  std::deque<Frame> sent;
};

uint16_t Get16(const Frame& f, size_t at) { return static_cast<uint16_t>(f[at] << 8 | f[at + 1]); }
void Put16(Frame& f, size_t at, uint16_t v) {
  f[at] = static_cast<uint8_t>(v >> 8);
  f[at + 1] = static_cast<uint8_t>(v);
}

constexpr net::Mac kMacA = {{0x02, 0, 0, 0, 0, 0xa}};
constexpr net::Mac kMacB = {{0x02, 0, 0, 0, 0, 0xb}};
constexpr net::Mac kMacRouter = {{0x02, 0, 0, 0, 0, 0x1}};
constexpr net::Ip4 kIpA = {{10, 0, 0, 10}};
constexpr net::Ip4 kIpB = {{10, 0, 0, 11}};
constexpr net::Ip4 kIpRouter = {{10, 0, 0, 1}};
constexpr net::Ip4 kMask = {{255, 255, 255, 0}};

// Ethernet + IPv4 frame around `payload`, with a valid header checksum.
Frame IpFrame(const net::Mac& from_mac, const net::Mac& to_mac, const net::Ip4& from, const net::Ip4& to,
              uint8_t protocol, const Frame& payload) {
  Frame f(34);
  std::memcpy(f.data(), to_mac.b, 6);
  std::memcpy(f.data() + 6, from_mac.b, 6);
  Put16(f, 12, 0x0800);
  f[14] = 0x45;
  Put16(f, 16, static_cast<uint16_t>(20 + payload.size()));
  f[22] = 64;
  f[23] = protocol;
  std::memcpy(f.data() + 26, from.b, 4);
  std::memcpy(f.data() + 30, to.b, 4);
  Put16(f, 24, net::ChecksumFinish(net::ChecksumAdd(0, f.data() + 14, 20)));
  f.insert(f.end(), payload.begin(), payload.end());
  return f;
}

// UDP frame with a valid checksum.
Frame UdpFrame(const net::Mac& from_mac, const net::Mac& to_mac, const net::Ip4& from, const net::Ip4& to,
               uint16_t from_port, uint16_t to_port, const Frame& data) {
  Frame u(8);
  Put16(u, 0, from_port);
  Put16(u, 2, to_port);
  Put16(u, 4, static_cast<uint16_t>(8 + data.size()));
  u.insert(u.end(), data.begin(), data.end());
  uint8_t pseudo[12] = {};
  std::memcpy(pseudo, from.b, 4);
  std::memcpy(pseudo + 4, to.b, 4);
  pseudo[9] = 17;
  pseudo[10] = static_cast<uint8_t>(u.size() >> 8);
  pseudo[11] = static_cast<uint8_t>(u.size());
  Put16(u, 6, net::ChecksumFinish(net::ChecksumAdd(net::ChecksumAdd(0, pseudo, 12), u.data(), u.size())));
  return IpFrame(from_mac, to_mac, from, to, 17, u);
}

// Checks the IPv4 header checksum and, for UDP, the UDP checksum of a frame the stack produced.
bool ChecksumsValid(const Frame& f) {
  if (Get16(f, 12) != 0x0800 || net::ChecksumFinish(net::ChecksumAdd(0, f.data() + 14, 20)) != 0) {
    return false;
  }
  if (f[23] != 17) {
    return true;
  }
  const size_t udp_length = Get16(f, 38);
  uint8_t pseudo[12] = {};
  std::memcpy(pseudo, f.data() + 26, 8);
  pseudo[9] = 17;
  pseudo[10] = static_cast<uint8_t>(udp_length >> 8);
  pseudo[11] = static_cast<uint8_t>(udp_length);
  return net::ChecksumFinish(net::ChecksumAdd(net::ChecksumAdd(0, pseudo, 12), f.data() + 34, udp_length)) == 0;
}

struct Received {
  std::vector<net::UdpDatagram> datagrams;
  std::vector<std::string> texts;
};

void Collect(void* context, const net::UdpDatagram& d) {
  auto* r = static_cast<Received*>(context);
  r->datagrams.push_back(d);
  r->texts.emplace_back(reinterpret_cast<const char*>(d.data), d.length);
}

struct Node {
  Node(const net::Mac& mac, const net::Ip4& ip) {
    stack.Init(&link, mac);
    stack.Configure({.ip = ip, .netmask = kMask, .gateway = kIpRouter});
  }
  FakeLink link;
  net::Stack stack;
};

// Delivers everything `from` sent to `to`; returns the number of frames.
int Deliver(Node& from, Node& to, uint64_t now_ms = 0) {
  int n = 0;
  while (!from.link.sent.empty()) {
    const Frame f = from.link.Take();
    to.stack.HandleFrame(f.data(), f.size(), now_ms);
    ++n;
  }
  return n;
}

}  // namespace

TEST(ChecksumMatchesRfc1071Example) {
  const uint8_t data[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};
  EXPECT_EQ(net::ChecksumFinish(net::ChecksumAdd(0, data, sizeof(data))), 0x220d);
  const uint8_t odd[] = {0x01, 0x02, 0x03};  // the last byte is the high half of a word: 0x0102 + 0x0300
  EXPECT_EQ(net::ChecksumFinish(net::ChecksumAdd(0, odd, sizeof(odd))), static_cast<uint16_t>(~0x0402));
}

TEST(AnswersArpRequestForItsAddress) {
  Node a(kMacA, kIpA);
  Frame request(42, 0);
  std::memset(request.data(), 0xff, 6);
  std::memcpy(request.data() + 6, kMacB.b, 6);
  Put16(request, 12, 0x0806);
  Put16(request, 14, 1);
  Put16(request, 16, 0x0800);
  request[18] = 6;
  request[19] = 4;
  Put16(request, 20, 1);
  std::memcpy(request.data() + 22, kMacB.b, 6);
  std::memcpy(request.data() + 28, kIpB.b, 4);
  std::memcpy(request.data() + 38, kIpA.b, 4);
  a.stack.HandleFrame(request.data(), request.size(), 0);
  EXPECT_EQ(a.link.sent.size(), 1u);
  const Frame reply = a.link.Take();
  EXPECT_EQ(reply.size(), 60u);  // padded to the Ethernet minimum
  EXPECT_TRUE(std::memcmp(reply.data(), kMacB.b, 6) == 0);
  EXPECT_EQ(Get16(reply, 12), 0x0806);
  EXPECT_EQ(Get16(reply, 20), 2);
  EXPECT_TRUE(std::memcmp(reply.data() + 22, kMacA.b, 6) == 0);
  EXPECT_TRUE(std::memcmp(reply.data() + 28, kIpA.b, 4) == 0);
  EXPECT_TRUE(std::memcmp(reply.data() + 32, kMacB.b, 6) == 0);
  EXPECT_TRUE(std::memcmp(reply.data() + 38, kIpB.b, 4) == 0);

  // A request for another address gets no answer.
  std::memcpy(request.data() + 38, kIpRouter.b, 4);
  a.stack.HandleFrame(request.data(), request.size(), 0);
  EXPECT_TRUE(a.link.sent.empty());
}

TEST(UdpBetweenTwoStacksResolvesArpFirst) {
  Node a(kMacA, kIpA);
  Node b(kMacB, kIpB);
  Received at_b, at_a;
  EXPECT_TRUE(b.stack.Bind(4000, Collect, &at_b));
  EXPECT_TRUE(a.stack.Bind(5000, Collect, &at_a));

  EXPECT_TRUE(a.stack.SendUdp(kIpB, 4000, 5000, "hello", 5));
  EXPECT_EQ(a.link.sent.size(), 1u);  // only the ARP request; the datagram waits
  EXPECT_EQ(Get16(a.link.sent.front(), 12), 0x0806);
  EXPECT_EQ(Deliver(a, b), 1);
  EXPECT_EQ(Deliver(b, a), 1);  // ARP reply -> a sends the parked datagram
  EXPECT_EQ(a.link.sent.size(), 1u);
  EXPECT_TRUE(ChecksumsValid(a.link.sent.front()));
  EXPECT_EQ(Deliver(a, b), 1);
  EXPECT_EQ(at_b.texts.size(), 1u);
  EXPECT_STREQ(at_b.texts[0], "hello");
  EXPECT_TRUE(at_b.datagrams[0].source_ip == kIpA);
  EXPECT_EQ(at_b.datagrams[0].source_port, 5000);

  // b learned a's MAC from the datagram: the answer goes out directly, without ARP.
  EXPECT_TRUE(b.stack.SendUdp(kIpA, 5000, 4000, "world!", 6));
  EXPECT_EQ(b.link.sent.size(), 1u);
  EXPECT_EQ(Get16(b.link.sent.front(), 12), 0x0800);
  EXPECT_EQ(Deliver(b, a), 1);
  EXPECT_EQ(at_a.texts.size(), 1u);
  EXPECT_STREQ(at_a.texts[0], "world!");

  // Nobody listens on 4001: dropped silently.
  EXPECT_TRUE(a.stack.SendUdp(kIpB, 4001, 5000, "x", 1));
  Deliver(a, b);
  EXPECT_EQ(at_b.texts.size(), 1u);
}

TEST(DropsCorruptedUdp) {
  Node a(kMacA, kIpA);
  Received r;
  a.stack.Bind(4000, Collect, &r);
  Frame f = UdpFrame(kMacB, kMacA, kIpB, kIpA, 1234, 4000, {'o', 'k'});
  a.stack.HandleFrame(f.data(), f.size(), 0);
  EXPECT_EQ(r.texts.size(), 1u);
  f[42] ^= 1;  // payload bit flip
  a.stack.HandleFrame(f.data(), f.size(), 0);
  EXPECT_EQ(r.texts.size(), 1u);
  f = UdpFrame(kMacB, kMacA, kIpB, {{10, 0, 0, 99}}, 1234, 4000, {'n', 'o'});  // not our address
  a.stack.HandleFrame(f.data(), f.size(), 0);
  EXPECT_EQ(r.texts.size(), 1u);
}

TEST(AnswersPing) {
  Node a(kMacA, kIpA);
  Frame echo = {8, 0, 0, 0, 0x12, 0x34, 0, 1, 'p', 'i', 'n', 'g'};
  Put16(echo, 2, net::ChecksumFinish(net::ChecksumAdd(0, echo.data(), echo.size())));
  const Frame request = IpFrame(kMacB, kMacA, kIpB, kIpA, 1, echo);
  a.stack.HandleFrame(request.data(), request.size(), 0);
  EXPECT_EQ(a.link.sent.size(), 1u);  // MAC learned from the request: no ARP needed
  const Frame reply = a.link.Take();
  EXPECT_TRUE(ChecksumsValid(reply));
  EXPECT_TRUE(std::memcmp(reply.data(), kMacB.b, 6) == 0);
  EXPECT_EQ(reply[23], 1);
  EXPECT_EQ(reply[34], 0);  // echo reply
  EXPECT_EQ(net::ChecksumFinish(net::ChecksumAdd(0, reply.data() + 34, echo.size())), 0);
  EXPECT_TRUE(std::memcmp(reply.data() + 38, echo.data() + 4, echo.size() - 4) == 0);
}

TEST(OffSubnetGoesThroughGateway) {
  Node a(kMacA, kIpA);
  EXPECT_TRUE(a.stack.SendUdp({{192, 168, 1, 5}}, 4000, 5000, "far", 3));
  const Frame arp = a.link.Take();
  EXPECT_EQ(Get16(arp, 12), 0x0806);
  EXPECT_TRUE(std::memcmp(arp.data() + 38, kIpRouter.b, 4) == 0);  // asks for the router, not the destination
}

TEST(RetriesArpThenGivesUp) {
  Node a(kMacA, kIpA);
  a.stack.Tick(1000);
  EXPECT_TRUE(a.stack.SendUdp(kIpB, 4000, 5000, "x", 1));
  a.link.sent.clear();
  a.stack.Tick(1200);
  EXPECT_TRUE(a.link.sent.empty());
  a.stack.Tick(1500);
  EXPECT_EQ(a.link.sent.size(), 1u);  // retry after 500 ms
  a.link.sent.clear();
  a.stack.Tick(4000);  // 3 s: the datagram is dropped, no more requests
  a.stack.Tick(5000);
  EXPECT_TRUE(a.link.sent.empty());
}

TEST(DhcpClientGetsAnAddress) {
  FakeLink link;
  net::Stack stack;
  stack.Init(&link, kMacA);
  stack.StartDhcp(0, 0x12345678);
  EXPECT_TRUE(!stack.configured());
  EXPECT_EQ(link.sent.size(), 1u);
  Frame discover = link.Take();
  EXPECT_TRUE(ChecksumsValid(discover));
  EXPECT_TRUE(std::memcmp(discover.data(), "\xff\xff\xff\xff\xff\xff", 6) == 0);
  EXPECT_EQ(Get16(discover, 36), 67);
  const size_t dhcp = 42;
  EXPECT_EQ(discover[dhcp], 1);
  EXPECT_EQ(discover[dhcp + 4], 0x12);
  EXPECT_EQ(discover[dhcp + 240], 53);
  EXPECT_EQ(discover[dhcp + 242], 1);  // DISCOVER

  const net::Ip4 server = {{10, 0, 0, 2}};
  auto reply = [&](uint8_t type) {
    Frame d(240, 0);
    d[0] = 2;
    d[1] = 1;
    d[2] = 6;
    d[4] = 0x12, d[5] = 0x34, d[6] = 0x56, d[7] = 0x78;
    std::memcpy(d.data() + 16, kIpA.b, 4);
    std::memcpy(d.data() + 28, kMacA.b, 6);
    d[236] = 99, d[237] = 130, d[238] = 83, d[239] = 99;
    const Frame options = {53, 1, type, 54, 4, 10, 0, 0, 2, 1, 4, 255, 255, 255, 0, 3, 4, 10, 0, 0, 1, 255};
    d.insert(d.end(), options.begin(), options.end());
    return UdpFrame(kMacRouter, {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}}, server, {{255, 255, 255, 255}}, 67, 68, d);
  };

  Frame offer = reply(2);
  stack.HandleFrame(offer.data(), offer.size(), 10);
  EXPECT_EQ(link.sent.size(), 1u);
  const Frame request = link.Take();
  EXPECT_EQ(request[dhcp + 242], 3);  // REQUEST, with the offered address and the server id
  bool has_requested = false, has_server = false;
  for (size_t i = dhcp + 240; i + 1 < request.size() && request[i] != 255; i += 2 + request[i + 1]) {
    has_requested |= request[i] == 50 && std::memcmp(request.data() + i + 2, kIpA.b, 4) == 0;
    has_server |= request[i] == 54 && std::memcmp(request.data() + i + 2, server.b, 4) == 0;
  }
  EXPECT_TRUE(has_requested && has_server);

  Frame ack = reply(5);
  stack.HandleFrame(ack.data(), ack.size(), 20);
  EXPECT_TRUE(stack.configured());
  EXPECT_TRUE(!stack.dhcp_running());
  EXPECT_TRUE(stack.config().ip == kIpA);
  EXPECT_TRUE(stack.config().netmask == kMask);
  EXPECT_TRUE(stack.config().gateway == kIpRouter);
  stack.Tick(10000);
  EXPECT_TRUE(link.sent.empty());  // bound: no more DHCP traffic
}

TEST(DhcpIgnoresOtherTransactionsAndRetries) {
  FakeLink link;
  net::Stack stack;
  stack.Init(&link, kMacA);
  stack.StartDhcp(0, 1);
  link.sent.clear();
  Frame d(244, 0);
  d[0] = 2;
  d[7] = 2;  // xid 2: someone else's
  std::memcpy(d.data() + 16, kIpA.b, 4);
  std::memcpy(d.data() + 28, kMacA.b, 6);
  d[236] = 99, d[237] = 130, d[238] = 83, d[239] = 99;
  d[240] = 53, d[241] = 1, d[242] = 2, d[243] = 255;
  const Frame offer = UdpFrame(kMacRouter, kMacA, kIpRouter, {{255, 255, 255, 255}}, 67, 68, d);
  stack.HandleFrame(offer.data(), offer.size(), 10);
  EXPECT_TRUE(link.sent.empty());
  stack.Tick(2500);
  EXPECT_EQ(link.sent.size(), 1u);  // DISCOVER again
}
