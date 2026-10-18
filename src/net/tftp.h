// TFTP client (RFC 1350, with the blksize option of RFC 2348) for fetching one file into memory: the hot-reload key
// gets scroller.efi from the server that network-booted the machine. Polled like the rest of net::Stack.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "net/stack.h"

namespace net {

class TftpClient {
 public:
  static constexpr uint16_t kServerPort = 69;
  static constexpr uint16_t kClientPort = 6969;
  static constexpr uint16_t kBlockSize = 1408;  // asked for; a server without the option sends 512-byte blocks
  static constexpr uint64_t kRetryMs = 500;
  static constexpr int kMaxRetries = 6;

  enum class State : uint8_t { kIdle, kRunning, kDone, kFailed };

  // Binds the client port on `stack` (once).
  bool Init(Stack* stack);
  // Starts reading `file` from `server` into `buffer`. A transfer already running is abandoned.
  void Start(const Ip4& server, const char* file, uint8_t* buffer, size_t capacity, uint64_t now_ms);
  // Back to idle (after the caller has taken the result).
  void Reset() { state_ = State::kIdle; }
  // Retransmits on timeouts; call regularly.
  void Tick(uint64_t now_ms);

  State state() const { return state_; }
  size_t size() const { return size_; }         // bytes received so far
  const char* error() const { return error_; }  // when kFailed

 private:
  static void OnDatagram(void* context, const UdpDatagram& datagram);
  void Handle(const UdpDatagram& datagram);
  void SendAck(uint16_t block);
  void Fail(const char* error);

  Stack* stack_ = nullptr;
  State state_ = State::kIdle;
  Ip4 server_;
  uint16_t server_port_ = kServerPort;  // the server answers from a new port (its transfer ID)
  bool have_server_port_ = false;
  uint8_t* buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t size_ = 0;
  uint16_t block_size_ = 512;
  uint16_t last_block_ = 0;       // last block received and acknowledged (0: none / the option acknowledgment)
  uint8_t last_packet_[64] = {};  // the request or last ACK, for retransmission
  size_t last_packet_length_ = 0;
  uint64_t last_sent_ms_ = 0;
  uint64_t now_ms_ = 0;
  int retries_ = 0;
  char error_[64] = "";
};

}  // namespace net
