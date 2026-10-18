#include "net/stack.h"

#include "lib/mem.h"

namespace net {
namespace {

constexpr size_t kEthHeader = 14;
constexpr size_t kIpHeader = 20;
constexpr size_t kUdpHeader = 8;
constexpr size_t kIpPayload = kEthHeader + kIpHeader;  // offset of the IPv4 payload in a frame
constexpr size_t kUdpData = kIpPayload + kUdpHeader;

constexpr uint16_t kEtherIp = 0x0800;
constexpr uint16_t kEtherArp = 0x0806;
constexpr uint8_t kProtoIcmp = 1;
constexpr uint8_t kProtoUdp = 17;

constexpr uint16_t kDhcpServerPort = 67;
constexpr uint16_t kDhcpClientPort = 68;
constexpr uint64_t kDhcpRetryMs = 2000;
constexpr uint64_t kArpRetryMs = 500;
constexpr uint64_t kArpGiveUpMs = 3000;

constexpr Mac kBroadcastMac = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
constexpr Ip4 kBroadcastIp = {{255, 255, 255, 255}};

uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t Get32(const uint8_t* p) { return uint32_t{p[0]} << 24 | uint32_t{p[1]} << 16 | uint32_t{p[2]} << 8 | p[3]; }
void Put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}
void Put32(uint8_t* p, uint32_t v) {
  Put16(p, static_cast<uint16_t>(v >> 16));
  Put16(p + 2, static_cast<uint16_t>(v));
}
Ip4 GetIp(const uint8_t* p) { return {{p[0], p[1], p[2], p[3]}}; }

// UDP checksum over the pseudo header and `udp` (header with a zero checksum field, plus data).
uint16_t UdpChecksum(const Ip4& source, const Ip4& destination, const uint8_t* udp, size_t length) {
  uint8_t pseudo[12];
  lib::Copy(pseudo, source.b, 4);
  lib::Copy(pseudo + 4, destination.b, 4);
  pseudo[8] = 0;
  pseudo[9] = kProtoUdp;
  Put16(pseudo + 10, static_cast<uint16_t>(length));
  const uint16_t sum = ChecksumFinish(ChecksumAdd(ChecksumAdd(0, pseudo, sizeof(pseudo)), udp, length));
  return sum == 0 ? 0xffff : sum;  // 0 means "no checksum"
}

}  // namespace

uint32_t ChecksumAdd(uint32_t sum, const uint8_t* data, size_t length) {
  // Callers pass even lengths except for the last block, so pairing bytes from the start of each block is enough.
  for (size_t i = 0; i + 1 < length; i += 2) {
    sum += Get16(data + i);
  }
  if ((length & 1) != 0) {
    sum += uint32_t{data[length - 1]} << 8;
  }
  return sum;
}

uint16_t ChecksumFinish(uint32_t sum) {
  while ((sum >> 16) != 0) {
    sum = (sum & 0xffff) + (sum >> 16);
  }
  return static_cast<uint16_t>(~sum);
}

void Stack::Init(Link* link, const Mac& mac) {
  link_ = link;
  mac_ = mac;
}

void Stack::Configure(const Config& config) {
  config_ = config;
  configured_ = true;
  dhcp_state_ = DhcpState::kOff;
}

void Stack::StartDhcp(uint64_t now_ms, uint32_t xid) {
  now_ms_ = now_ms;
  dhcp_xid_ = xid;
  dhcp_state_ = DhcpState::kSelecting;
  SendDhcp(1);  // DISCOVER
}

bool Stack::Bind(uint16_t port, UdpHandler handler, void* context) {
  for (Binding& b : bindings_) {
    if (b.handler == nullptr) {
      b = {.port = port, .handler = handler, .context = context};
      return true;
    }
  }
  return false;
}

void Stack::Tick(uint64_t now_ms) {
  now_ms_ = now_ms;
  if (dhcp_running() && now_ms - dhcp_sent_ms_ >= kDhcpRetryMs) {
    // Start over from DISCOVER: the offer may have gone to someone else meanwhile.
    dhcp_state_ = DhcpState::kSelecting;
    SendDhcp(1);
  }
  if (pending_length_ != 0) {
    if (now_ms - pending_since_ms_ >= kArpGiveUpMs) {
      pending_length_ = 0;
    } else if (now_ms - arp_sent_ms_ >= kArpRetryMs) {
      SendArpRequest(pending_next_hop_);
    }
  }
}

void Stack::HandleFrame(const uint8_t* frame, size_t length, uint64_t now_ms) {
  now_ms_ = now_ms;
  if (length < kEthHeader) {
    return;
  }
  ++frames_received_;
  const uint16_t type = Get16(frame + 12);
  if (type == kEtherArp) {
    HandleArp(frame + kEthHeader, length - kEthHeader);
  } else if (type == kEtherIp) {
    HandleIp(frame + 6, frame + kEthHeader, length - kEthHeader);
  }
}

void Stack::HandleArp(const uint8_t* p, size_t length) {
  // Ethernet/IPv4 ARP only: htype 1, ptype 0x0800, hlen 6, plen 4.
  if (length < 28 || Get16(p) != 1 || Get16(p + 2) != kEtherIp || p[4] != 6 || p[5] != 4) {
    return;
  }
  const uint16_t op = Get16(p + 6);
  const Ip4 sender_ip = GetIp(p + 14);
  const Ip4 target_ip = GetIp(p + 24);
  if (!configured_) {
    return;
  }
  if (target_ip == config_.ip || Lookup(sender_ip) != nullptr) {
    Learn(sender_ip, p + 8);
  }
  if (op == 1 && target_ip == config_.ip) {
    uint8_t* a = tx_ + kEthHeader;
    Put16(a, 1);
    Put16(a + 2, kEtherIp);
    a[4] = 6;
    a[5] = 4;
    Put16(a + 6, 2);  // reply
    lib::Copy(a + 8, mac_.b, 6);
    lib::Copy(a + 14, config_.ip.b, 4);
    lib::Copy(a + 18, p + 8, 6);
    lib::Copy(a + 24, p + 14, 4);
    Mac to;
    lib::Copy(to.b, p + 8, 6);
    SendFrame(to, kEtherArp, 28);
  }
}

void Stack::HandleIp(const uint8_t* source_mac, const uint8_t* p, size_t length) {
  if (length < kIpHeader || (p[0] >> 4) != 4) {
    return;
  }
  const size_t header = (p[0] & 0xf) * 4u;
  const size_t total = Get16(p + 2);
  if (header < kIpHeader || total < header || total > length || ChecksumFinish(ChecksumAdd(0, p, header)) != 0) {
    return;
  }
  if ((Get16(p + 6) & 0x3fff) != 0) {
    return;  // a fragment (more-fragments flag or an offset): not supported
  }
  const Ip4 source = GetIp(p + 12);
  const Ip4 destination = GetIp(p + 16);
  const bool for_us = configured_ ? destination == config_.ip || IsBroadcast(destination)
                                  : destination == kBroadcastIp || dhcp_running();  // DHCP offers may be unicast
  if (!for_us) {
    return;
  }
  if (configured_ && OnLocalSubnet(source)) {
    Learn(source, source_mac);  // saves an ARP round trip when answering
  }
  if (p[9] == kProtoIcmp && configured_) {
    HandleIcmp(source, p + header, total - header);
  } else if (p[9] == kProtoUdp) {
    HandleUdp(source, destination, p + header, total - header);
  }
}

void Stack::HandleIcmp(const Ip4& source, const uint8_t* p, size_t length) {
  if (length < 8 || p[0] != 8 || ChecksumFinish(ChecksumAdd(0, p, length)) != 0 || length > kMaxFrame - kIpPayload) {
    return;  // only echo requests
  }
  uint8_t* reply = tx_ + kIpPayload;
  lib::Copy(reply, p, length);
  reply[0] = 0;  // echo reply
  reply[2] = reply[3] = 0;
  Put16(reply + 2, ChecksumFinish(ChecksumAdd(0, reply, length)));
  SendIp(source, kProtoIcmp, length);
}

void Stack::HandleUdp(const Ip4& source, const Ip4& destination, const uint8_t* p, size_t length) {
  if (length < kUdpHeader) {
    return;
  }
  const size_t udp_length = Get16(p + 4);
  if (udp_length < kUdpHeader || udp_length > length) {
    return;
  }
  if (Get16(p + 6) != 0) {
    // Verify: the sum over pseudo header and datagram including its checksum field must come out as 0.
    uint8_t pseudo[12];
    lib::Copy(pseudo, source.b, 4);
    lib::Copy(pseudo + 4, destination.b, 4);
    pseudo[8] = 0;
    pseudo[9] = kProtoUdp;
    Put16(pseudo + 10, static_cast<uint16_t>(udp_length));
    if (ChecksumFinish(ChecksumAdd(ChecksumAdd(0, pseudo, sizeof(pseudo)), p, udp_length)) != 0) {
      return;
    }
  }
  const UdpDatagram datagram = {.source_ip = source,
                                .source_port = Get16(p),
                                .destination_port = Get16(p + 2),
                                .data = p + kUdpHeader,
                                .length = udp_length - kUdpHeader};
  if (datagram.destination_port == kDhcpClientPort && datagram.source_port == kDhcpServerPort) {
    HandleDhcp(datagram);
    return;
  }
  if (!configured_) {
    return;
  }
  for (const Binding& b : bindings_) {
    if (b.handler != nullptr && b.port == datagram.destination_port) {
      b.handler(b.context, datagram);
      return;
    }
  }
}

bool Stack::SendUdp(const Ip4& destination, uint16_t destination_port, uint16_t source_port, const void* data,
                    size_t length) {
  if (!configured_ || length > kMaxUdpPayload) {
    return false;
  }
  uint8_t* u = tx_ + kIpPayload;
  Put16(u, source_port);
  Put16(u + 2, destination_port);
  Put16(u + 4, static_cast<uint16_t>(kUdpHeader + length));
  Put16(u + 6, 0);
  lib::Copy(u + kUdpHeader, data, length);
  Put16(u + 6, UdpChecksum(config_.ip, destination, u, kUdpHeader + length));
  return SendIp(destination, kProtoUdp, kUdpHeader + length, IsBroadcast(destination));
}

bool Stack::SendIp(const Ip4& destination, uint8_t protocol, size_t payload_length, bool broadcast) {
  uint8_t* ip = tx_ + kEthHeader;
  ip[0] = 0x45;  // version 4, 20-byte header
  ip[1] = 0;
  Put16(ip + 2, static_cast<uint16_t>(kIpHeader + payload_length));
  Put16(ip + 4, ip_id_++);
  Put16(ip + 6, 0x4000);  // don't fragment
  ip[8] = 64;             // TTL
  ip[9] = protocol;
  Put16(ip + 10, 0);
  lib::Copy(ip + 12, configured_ ? config_.ip.b : Ip4{}.b, 4);
  lib::Copy(ip + 16, destination.b, 4);
  Put16(ip + 10, ChecksumFinish(ChecksumAdd(0, ip, kIpHeader)));
  if (broadcast) {
    return SendFrame(kBroadcastMac, kEtherIp, kIpHeader + payload_length);
  }
  const Ip4 next_hop = OnLocalSubnet(destination) || config_.gateway.IsZero() ? destination : config_.gateway;
  if (const Mac* mac = Lookup(next_hop)) {
    return SendFrame(*mac, kEtherIp, kIpHeader + payload_length);
  }
  // Park the frame (Ethernet header without the destination) until the ARP reply arrives.
  lib::Copy(tx_ + 6, mac_.b, 6);
  Put16(tx_ + 12, kEtherIp);
  pending_length_ = kIpPayload + payload_length;
  lib::Copy(pending_, tx_, pending_length_);
  if (pending_length_ < 60) {
    lib::Fill(pending_ + pending_length_, 0, 60 - pending_length_);
    pending_length_ = 60;
  }
  pending_next_hop_ = next_hop;
  pending_since_ms_ = now_ms_;
  SendArpRequest(next_hop);
  return true;
}

bool Stack::SendFrame(const Mac& destination, uint16_t ether_type, size_t payload_length) {
  lib::Copy(tx_, destination.b, 6);
  lib::Copy(tx_ + 6, mac_.b, 6);
  Put16(tx_ + 12, ether_type);
  size_t length = kEthHeader + payload_length;
  if (length < 60) {
    lib::Fill(tx_ + length, 0, 60 - length);  // minimum frame size; most NICs pad, but not all
    length = 60;
  }
  if (link_ == nullptr || !link_->Send(tx_, length)) {
    return false;
  }
  ++frames_sent_;
  return true;
}

void Stack::SendArpRequest(const Ip4& target) {
  uint8_t* a = tx_ + kEthHeader;
  Put16(a, 1);
  Put16(a + 2, kEtherIp);
  a[4] = 6;
  a[5] = 4;
  Put16(a + 6, 1);  // request
  lib::Copy(a + 8, mac_.b, 6);
  lib::Copy(a + 14, config_.ip.b, 4);
  lib::Fill(a + 18, 0, 6);
  lib::Copy(a + 24, target.b, 4);
  arp_sent_ms_ = now_ms_;
  SendFrame(kBroadcastMac, kEtherArp, 28);
}

void Stack::Learn(const Ip4& ip, const uint8_t* mac) {
  ArpEntry* entry = nullptr;
  for (ArpEntry& e : arp_) {
    if (e.valid && e.ip == ip) {
      entry = &e;
      break;
    }
  }
  if (entry == nullptr) {
    entry = &arp_[arp_next_];
    arp_next_ = (arp_next_ + 1) % kArpEntries;
  }
  entry->ip = ip;
  lib::Copy(entry->mac.b, mac, 6);
  entry->valid = true;
  if (pending_length_ != 0 && pending_next_hop_ == ip) {
    lib::Copy(pending_, mac, 6);
    if (link_ != nullptr && link_->Send(pending_, pending_length_)) {
      ++frames_sent_;
    }
    pending_length_ = 0;
  }
}

const Mac* Stack::Lookup(const Ip4& ip) const {
  for (const ArpEntry& e : arp_) {
    if (e.valid && e.ip == ip) {
      return &e.mac;
    }
  }
  return nullptr;
}

bool Stack::OnLocalSubnet(const Ip4& ip) const {
  const uint32_t mask = config_.netmask.value();
  return (ip.value() & mask) == (config_.ip.value() & mask);
}

bool Stack::IsBroadcast(const Ip4& ip) const {
  const uint32_t mask = config_.netmask.value();
  return ip == kBroadcastIp || (mask != 0 && mask != 0xffffffff && OnLocalSubnet(ip) && (ip.value() | mask) == ~0u);
}

// DHCP (RFC 2131): DISCOVER -> OFFER -> REQUEST -> ACK, everything broadcast. The lease is never renewed: a debug
// session is much shorter than any lease.
void Stack::SendDhcp(uint8_t message_type) {
  uint8_t* d = tx_ + kUdpData;
  lib::Fill(d, 0, 236);
  d[0] = 1;  // BOOTREQUEST
  d[1] = 1;  // Ethernet
  d[2] = 6;
  Put32(d + 4, dhcp_xid_);
  Put16(d + 10, 0x8000);  // broadcast flag: we cannot receive unicast to an address we do not have yet
  lib::Copy(d + 28, mac_.b, 6);
  uint8_t* o = d + 236;
  Put32(o, 0x63825363);  // magic cookie
  o += 4;
  *o++ = 53;  // message type
  *o++ = 1;
  *o++ = message_type;
  if (message_type == 3) {
    *o++ = 50;  // requested address
    *o++ = 4;
    lib::Copy(o, dhcp_offered_.b, 4);
    o += 4;
    *o++ = 54;  // server identifier
    *o++ = 4;
    lib::Copy(o, dhcp_server_.b, 4);
    o += 4;
  }
  *o++ = 55;  // parameter request list: subnet mask, router
  *o++ = 2;
  *o++ = 1;
  *o++ = 3;
  *o++ = 255;
  const size_t length = static_cast<size_t>(o - d);

  uint8_t* u = tx_ + kIpPayload;
  Put16(u, kDhcpClientPort);
  Put16(u + 2, kDhcpServerPort);
  Put16(u + 4, static_cast<uint16_t>(kUdpHeader + length));
  Put16(u + 6, 0);
  Put16(u + 6, UdpChecksum(Ip4{}, kBroadcastIp, u, kUdpHeader + length));
  const bool was_configured = configured_;
  configured_ = false;  // source address 0.0.0.0
  SendIp(kBroadcastIp, kProtoUdp, kUdpHeader + length, true);
  configured_ = was_configured;
  dhcp_sent_ms_ = now_ms_;
}

void Stack::HandleDhcp(const UdpDatagram& datagram) {
  const uint8_t* d = datagram.data;
  if (!dhcp_running() || datagram.length < 240 || d[0] != 2 || Get32(d + 4) != dhcp_xid_ ||
      Get32(d + 236) != 0x63825363 || !(Mac{{d[28], d[29], d[30], d[31], d[32], d[33]}} == mac_)) {
    return;
  }
  uint8_t type = 0;
  Ip4 server, netmask, router;
  for (size_t i = 240; i < datagram.length;) {
    const uint8_t code = d[i];
    if (code == 255) {
      break;
    }
    if (code == 0) {
      ++i;
      continue;
    }
    if (i + 2 > datagram.length || i + 2 + d[i + 1] > datagram.length) {
      return;
    }
    const uint8_t len = d[i + 1];
    const uint8_t* value = d + i + 2;
    if (code == 53 && len >= 1) {
      type = value[0];
    } else if (code == 54 && len >= 4) {
      server = GetIp(value);
    } else if (code == 1 && len >= 4) {
      netmask = GetIp(value);
    } else if (code == 3 && len >= 4) {
      router = GetIp(value);
    }
    i += 2u + len;
  }
  const Ip4 offered = GetIp(d + 16);                                             // yiaddr
  if (dhcp_state_ == DhcpState::kSelecting && type == 2 && !offered.IsZero()) {  // OFFER
    dhcp_offered_ = offered;
    dhcp_server_ = server;
    dhcp_state_ = DhcpState::kRequesting;
    SendDhcp(3);                                                                                // REQUEST
  } else if (dhcp_state_ == DhcpState::kRequesting && type == 5 && offered == dhcp_offered_) {  // ACK
    dhcp_state_ = DhcpState::kBound;
    config_ = {.ip = offered, .netmask = netmask.IsZero() ? Ip4{{255, 255, 255, 0}} : netmask, .gateway = router};
    configured_ = true;
  } else if (dhcp_state_ == DhcpState::kRequesting && type == 6) {  // NAK
    dhcp_state_ = DhcpState::kSelecting;
    SendDhcp(1);
  }
}

}  // namespace net
