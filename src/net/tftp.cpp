#include "net/tftp.h"

#include "lib/format.h"
#include "lib/mem.h"

namespace net {
namespace {

constexpr uint16_t kRrq = 1, kData = 3, kAck = 4, kError = 5, kOack = 6;

uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
void Put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

// Appends a NUL-terminated string; returns the new length (stops at `capacity`).
size_t PutString(uint8_t* out, size_t at, size_t capacity, const char* s) {
  while (*s != '\0' && at + 1 < capacity) {
    out[at++] = static_cast<uint8_t>(*s++);
  }
  out[at++] = 0;
  return at;
}

}  // namespace

bool TftpClient::Init(Stack* stack) {
  stack_ = stack;
  return stack->Bind(kClientPort, &TftpClient::OnDatagram, this);
}

void TftpClient::Start(const Ip4& server, const char* file, uint8_t* buffer, size_t capacity, uint64_t now_ms) {
  state_ = State::kRunning;
  server_ = server;
  server_port_ = kServerPort;
  have_server_port_ = false;
  buffer_ = buffer;
  capacity_ = capacity;
  size_ = 0;
  block_size_ = 512;
  last_block_ = 0;
  retries_ = 0;
  error_[0] = '\0';
  now_ms_ = now_ms;

  char block_size[8];
  lib::Format(block_size, sizeof(block_size), "%u", kBlockSize);
  uint8_t* p = last_packet_;
  Put16(p, kRrq);
  size_t n = 2;
  n = PutString(p, n, sizeof(last_packet_), file);
  n = PutString(p, n, sizeof(last_packet_), "octet");
  n = PutString(p, n, sizeof(last_packet_), "blksize");
  n = PutString(p, n, sizeof(last_packet_), block_size);
  last_packet_length_ = n;
  last_sent_ms_ = now_ms;
  stack_->SendUdp(server_, kServerPort, kClientPort, last_packet_, last_packet_length_);
}

void TftpClient::Tick(uint64_t now_ms) {
  now_ms_ = now_ms;
  if (state_ != State::kRunning || now_ms - last_sent_ms_ < kRetryMs) {
    return;
  }
  if (++retries_ > kMaxRetries) {
    Fail(size_ == 0 ? "no answer from the TFTP server" : "transfer stalled");
    return;
  }
  last_sent_ms_ = now_ms;
  // Before the server's first answer the request goes to port 69, afterwards ACKs go to its transfer port.
  stack_->SendUdp(server_, server_port_, kClientPort, last_packet_, last_packet_length_);
}

void TftpClient::OnDatagram(void* context, const UdpDatagram& datagram) {
  static_cast<TftpClient*>(context)->Handle(datagram);
}

void TftpClient::SendAck(uint16_t block) {
  Put16(last_packet_, kAck);
  Put16(last_packet_ + 2, block);
  last_packet_length_ = 4;
  last_sent_ms_ = now_ms_;
  retries_ = 0;
  stack_->SendUdp(server_, server_port_, kClientPort, last_packet_, last_packet_length_);
}

void TftpClient::Fail(const char* error) {
  state_ = State::kFailed;
  lib::Format(error_, sizeof(error_), "%s", error);
}

void TftpClient::Handle(const UdpDatagram& d) {
  if (state_ != State::kRunning || !(d.source_ip == server_) || d.length < 4) {
    return;
  }
  if (have_server_port_ && d.source_port != server_port_) {
    return;  // a stray packet from another transfer
  }
  const uint16_t opcode = Get16(d.data);
  if (opcode == kError) {
    char message[48] = "server: ";
    size_t n = 8;
    for (size_t i = 4; i < d.length && d.data[i] != 0 && n + 1 < sizeof(message); ++i) {
      message[n++] = static_cast<char>(d.data[i]);
    }
    message[n] = '\0';
    Fail(message);
    return;
  }
  if (!have_server_port_) {
    server_port_ = d.source_port;
    have_server_port_ = true;
  }
  if (opcode == kOack) {
    // Options the server accepted: "name\0value\0" pairs. Only blksize matters.
    for (size_t at = 2; at < d.length;) {
      const char* name = reinterpret_cast<const char*>(d.data + at);
      size_t name_end = at;
      while (name_end < d.length && d.data[name_end] != 0) {
        ++name_end;
      }
      size_t value_end = name_end + 1;
      while (value_end < d.length && d.data[value_end] != 0) {
        ++value_end;
      }
      if (name_end - at == 7 && __builtin_memcmp(name, "blksize", 7) == 0) {
        uint32_t value = 0;
        for (size_t i = name_end + 1; i < value_end && i < d.length; ++i) {
          value = value * 10 + static_cast<uint32_t>(d.data[i] - '0');
        }
        if (value >= 8 && value <= kBlockSize) {
          block_size_ = static_cast<uint16_t>(value);
        }
      }
      at = value_end + 1;
    }
    SendAck(0);
    return;
  }
  if (opcode != kData) {
    return;
  }
  const uint16_t block = Get16(d.data + 2);
  const size_t length = d.length - 4;
  if (block == static_cast<uint16_t>(last_block_ + 1)) {
    if (length > capacity_ - size_) {
      Fail("file larger than the buffer");
      return;
    }
    lib::Copy(buffer_ + size_, d.data + 4, length);
    size_ += length;
    last_block_ = block;
    SendAck(block);
    if (length < block_size_) {
      state_ = State::kDone;
    }
  } else if (block == last_block_) {
    SendAck(block);  // our ACK was lost: the server sent the block again
  }
}

}  // namespace net
