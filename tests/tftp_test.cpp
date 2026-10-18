#include "net/tftp.h"

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
  std::deque<Frame> sent;
};

constexpr net::Ip4 kClientIp = {{10, 0, 0, 10}};
constexpr net::Ip4 kServerIp = {{10, 0, 0, 2}};
constexpr uint16_t kTransferPort = 40000;  // the server's transfer ID

struct Received {
  std::vector<net::UdpDatagram> datagrams;
  std::vector<std::vector<uint8_t>> payloads;
};

void Collect(void* context, const net::UdpDatagram& d) {
  auto* r = static_cast<Received*>(context);
  r->datagrams.push_back(d);
  r->payloads.emplace_back(d.data, d.data + d.length);
}

// The client's stack and a server stack, connected by delivering frames by hand.
struct Fixture {
  Fixture() {
    client_stack.Init(&client_link, {{2, 0, 0, 0, 0, 0xa}});
    client_stack.Configure({.ip = kClientIp, .netmask = {{255, 255, 255, 0}}, .gateway = {}});
    server_stack.Init(&server_link, {{2, 0, 0, 0, 0, 0xb}});
    server_stack.Configure({.ip = kServerIp, .netmask = {{255, 255, 255, 0}}, .gateway = {}});
    server_stack.Bind(69, Collect, &at_69);
    server_stack.Bind(kTransferPort, Collect, &at_transfer);
    tftp.Init(&client_stack);
  }
  // Moves frames both ways until nothing is left.
  void Exchange(uint64_t now_ms = 0) {
    while (!client_link.sent.empty() || !server_link.sent.empty()) {
      while (!client_link.sent.empty()) {
        Frame f = client_link.sent.front();
        client_link.sent.pop_front();
        server_stack.HandleFrame(f.data(), f.size(), now_ms);
      }
      while (!server_link.sent.empty()) {
        Frame f = server_link.sent.front();
        server_link.sent.pop_front();
        client_stack.HandleFrame(f.data(), f.size(), now_ms);
      }
    }
  }
  void ServerSend(const std::vector<uint8_t>& packet) {
    server_stack.SendUdp(kClientIp, net::TftpClient::kClientPort, kTransferPort, packet.data(), packet.size());
  }
  static std::vector<uint8_t> Data(uint16_t block, const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> p = {0, 3, static_cast<uint8_t>(block >> 8), static_cast<uint8_t>(block)};
    p.insert(p.end(), bytes.begin(), bytes.end());
    return p;
  }
  static uint16_t AckBlock(const std::vector<uint8_t>& p) {
    return p.size() == 4 && p[1] == 4 ? static_cast<uint16_t>(p[2] << 8 | p[3]) : 0xffff;
  }

  FakeLink client_link, server_link;
  net::Stack client_stack, server_stack;
  net::TftpClient tftp;
  Received at_69, at_transfer;
  std::vector<uint8_t> buffer = std::vector<uint8_t>(10000);
};

std::vector<uint8_t> Pattern(size_t n, uint8_t seed) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; ++i) {
    v[i] = static_cast<uint8_t>(seed + i * 7);
  }
  return v;
}

}  // namespace

TEST(RequestAsksForTheFileWithBlksize) {
  Fixture f;
  f.tftp.Start(kServerIp, "scroller.efi", f.buffer.data(), f.buffer.size(), 0);
  f.Exchange();
  EXPECT_EQ(f.at_69.payloads.size(), 1u);
  const std::vector<uint8_t>& rrq = f.at_69.payloads[0];
  const std::string text(rrq.begin() + 2, rrq.end());
  EXPECT_TRUE(rrq[0] == 0 && rrq[1] == 1);
  EXPECT_TRUE(text == std::string("scroller.efi\0octet\0blksize\0"
                                  "1408\0",
                                  32));
  EXPECT_EQ(f.at_69.datagrams[0].source_port, net::TftpClient::kClientPort);
}

TEST(TransfersWithNegotiatedBlockSize) {
  Fixture f;
  f.tftp.Start(kServerIp, "scroller.efi", f.buffer.data(), f.buffer.size(), 0);
  f.Exchange();
  const std::vector<uint8_t> oack = {0, 6, 'b', 'l', 'k', 's', 'i', 'z', 'e', 0, '1', '4', '0', '8', 0};
  f.ServerSend(oack);
  f.Exchange();
  EXPECT_EQ(Fixture::AckBlock(f.at_transfer.payloads.back()), 0);  // option acknowledgment acknowledged
  const std::vector<uint8_t> file = Pattern(1408 * 2 + 100, 3);
  for (uint16_t block = 1; block <= 3; ++block) {
    const size_t from = (block - 1) * 1408u;
    const size_t to = std::min(file.size(), from + 1408);
    f.ServerSend(Fixture::Data(block, std::vector<uint8_t>(file.begin() + from, file.begin() + to)));
    f.Exchange();
    EXPECT_EQ(Fixture::AckBlock(f.at_transfer.payloads.back()), block);
  }
  EXPECT_TRUE(f.tftp.state() == net::TftpClient::State::kDone);
  EXPECT_EQ(f.tftp.size(), file.size());
  EXPECT_TRUE(std::memcmp(f.buffer.data(), file.data(), file.size()) == 0);
}

TEST(WithoutOptionsUses512ByteBlocksAndHandlesDuplicates) {
  Fixture f;
  f.tftp.Start(kServerIp, "x", f.buffer.data(), f.buffer.size(), 0);
  f.Exchange();
  const std::vector<uint8_t> file = Pattern(512 + 10, 9);
  f.ServerSend(Fixture::Data(1, std::vector<uint8_t>(file.begin(), file.begin() + 512)));
  f.Exchange();
  f.ServerSend(Fixture::Data(1, std::vector<uint8_t>(file.begin(), file.begin() + 512)));  // our ACK "was lost"
  f.Exchange();
  EXPECT_EQ(Fixture::AckBlock(f.at_transfer.payloads.back()), 1);
  EXPECT_TRUE(f.tftp.state() == net::TftpClient::State::kRunning);
  f.ServerSend(Fixture::Data(2, std::vector<uint8_t>(file.begin() + 512, file.end())));
  f.Exchange();
  EXPECT_TRUE(f.tftp.state() == net::TftpClient::State::kDone);
  EXPECT_EQ(f.tftp.size(), file.size());
}

TEST(ServerErrorAndTimeoutFail) {
  Fixture f;
  f.tftp.Start(kServerIp, "missing.efi", f.buffer.data(), f.buffer.size(), 0);
  f.Exchange();
  f.ServerSend({0, 5, 0, 1, 'n', 'o', 't', ' ', 'f', 'o', 'u', 'n', 'd', 0});
  f.Exchange();
  EXPECT_TRUE(f.tftp.state() == net::TftpClient::State::kFailed);
  EXPECT_STREQ(f.tftp.error(), "server: not found");

  Fixture g;
  g.tftp.Start(kServerIp, "x", g.buffer.data(), g.buffer.size(), 0);
  g.Exchange();
  for (uint64_t ms = 500; ms <= 4000; ms += 500) {
    g.tftp.Tick(ms);
    g.Exchange(ms);
  }
  EXPECT_EQ(g.at_69.payloads.size(), 7u);  // the request and 6 retries
  EXPECT_TRUE(g.tftp.state() == net::TftpClient::State::kFailed);
}
