// Minimal IPv4 network stack for the debug link: Ethernet II, ARP, IPv4 (no fragmentation, no options), ICMP echo
// (ping), UDP, and a DHCP client for when the firmware did not hand over an address.
//
// Single-threaded and allocation-free: the main loop passes every received frame to HandleFrame and calls Tick
// regularly; replies and SendUdp go out synchronously through the Link. Platform independent (host-tested).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace net {

struct Mac {
  uint8_t b[6] = {};
  bool operator==(const Mac&) const = default;
};

struct Ip4 {
  uint8_t b[4] = {};
  bool operator==(const Ip4&) const = default;
  bool IsZero() const { return (b[0] | b[1] | b[2] | b[3]) == 0; }
  uint32_t value() const { return uint32_t{b[0]} << 24 | uint32_t{b[1]} << 16 | uint32_t{b[2]} << 8 | b[3]; }
  static Ip4 FromValue(uint32_t v) {
    return {{static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8),
             static_cast<uint8_t>(v)}};
  }
};

inline constexpr size_t kMaxFrame = 1514;                // Ethernet header + 1500 bytes payload, without FCS
inline constexpr size_t kMaxUdpPayload = 1500 - 20 - 8;  // 1472

// What the stack needs from a network driver.
class Link {
 public:
  // Queues one complete Ethernet frame (destination MAC first, no FCS) for sending. False if it was dropped.
  virtual bool Send(const uint8_t* frame, size_t length) = 0;

 protected:
  ~Link() = default;
};

struct Config {
  Ip4 ip;
  Ip4 netmask;
  Ip4 gateway;  // zero: only the local subnet is reachable
};

struct UdpDatagram {
  Ip4 source_ip;
  uint16_t source_port = 0;
  uint16_t destination_port = 0;
  const uint8_t* data = nullptr;
  size_t length = 0;
};

// Internet checksum (RFC 1071) of `length` bytes, continuing from `sum` (ones' complement sum, not yet inverted).
uint32_t ChecksumAdd(uint32_t sum, const uint8_t* data, size_t length);
uint16_t ChecksumFinish(uint32_t sum);

class Stack {
 public:
  using UdpHandler = void (*)(void* context, const UdpDatagram& datagram);

  static constexpr int kMaxBindings = 4;
  static constexpr int kArpEntries = 8;

  void Init(Link* link, const Mac& mac);
  // Static address (e.g. from the PXE boot); stops a DHCP client that is running.
  void Configure(const Config& config);
  // Asks for an address by DHCP (broadcast). `xid` should differ between boots. Retries from Tick until bound.
  void StartDhcp(uint64_t now_ms, uint32_t xid);

  // Processes one received frame (Ethernet header first).
  void HandleFrame(const uint8_t* frame, size_t length, uint64_t now_ms);
  // Retransmits DHCP and ARP requests, drops packets that waited too long for an ARP reply.
  void Tick(uint64_t now_ms);

  // Datagrams to `port` go to `handler`. False if all slots are taken.
  bool Bind(uint16_t port, UdpHandler handler, void* context);
  // Sends a datagram (at most kMaxUdpPayload bytes). If the destination's MAC is not known yet, an ARP request goes
  // out and the datagram waits in a single slot (a newer one replaces it). False if not configured or too large.
  bool SendUdp(const Ip4& destination, uint16_t destination_port, uint16_t source_port, const void* data,
               size_t length);

  bool configured() const { return configured_; }
  bool dhcp_running() const { return dhcp_state_ != DhcpState::kOff && dhcp_state_ != DhcpState::kBound; }
  const Config& config() const { return config_; }
  const Mac& mac() const { return mac_; }
  uint64_t frames_received() const { return frames_received_; }
  uint64_t frames_sent() const { return frames_sent_; }

 private:
  enum class DhcpState : uint8_t { kOff, kSelecting, kRequesting, kBound };

  struct ArpEntry {
    Ip4 ip;
    Mac mac;
    bool valid = false;
  };

  struct Binding {
    uint16_t port = 0;
    UdpHandler handler = nullptr;
    void* context = nullptr;
  };

  void HandleArp(const uint8_t* packet, size_t length);
  void HandleIp(const uint8_t* source_mac, const uint8_t* packet, size_t length);
  void HandleIcmp(const Ip4& source, const uint8_t* packet, size_t length);
  void HandleUdp(const Ip4& source, const Ip4& destination, const uint8_t* packet, size_t length);
  void HandleDhcp(const UdpDatagram& datagram);

  void Learn(const Ip4& ip, const uint8_t* mac);
  const Mac* Lookup(const Ip4& ip) const;
  bool OnLocalSubnet(const Ip4& ip) const;
  bool IsBroadcast(const Ip4& ip) const;
  void SendArpRequest(const Ip4& target);
  void SendDhcp(uint8_t message_type);

  // Sends the IPv4 payload already written at tx_ + kIpPayload: adds the IPv4 and Ethernet headers and resolves the
  // next hop (broadcast: to everyone, from our address or 0.0.0.0 while it is unknown).
  bool SendIp(const Ip4& destination, uint8_t protocol, size_t payload_length, bool broadcast = false);
  bool SendFrame(const Mac& destination, uint16_t ether_type, size_t payload_length);  // payload already in tx_

  Link* link_ = nullptr;
  Mac mac_;
  Config config_;
  bool configured_ = false;
  uint16_t ip_id_ = 0;
  uint64_t now_ms_ = 0;

  ArpEntry arp_[kArpEntries];
  int arp_next_ = 0;  // round-robin replacement

  // One datagram waiting for an ARP reply: a complete frame with the destination MAC still to fill in.
  uint8_t pending_[kMaxFrame] = {};
  size_t pending_length_ = 0;
  Ip4 pending_next_hop_;
  uint64_t pending_since_ms_ = 0;
  uint64_t arp_sent_ms_ = 0;

  Binding bindings_[kMaxBindings];

  DhcpState dhcp_state_ = DhcpState::kOff;
  uint32_t dhcp_xid_ = 0;
  uint64_t dhcp_sent_ms_ = 0;
  Ip4 dhcp_offered_;
  Ip4 dhcp_server_;

  uint8_t tx_[kMaxFrame] = {};
  uint64_t frames_received_ = 0;
  uint64_t frames_sent_ = 0;
};

}  // namespace net
