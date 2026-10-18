#include "drivers/pic.h"

#include "arch/x86_64/io.h"

namespace pic {
namespace {

void Out(uint16_t port, uint8_t value) {
  x86::Outb(port, value);
  x86::Outb(0x80, 0);  // short delay for old controllers
}

}  // namespace

void RemapAndMaskAll() {
  Out(0x20, 0x11);  // ICW1: initialise, expect ICW4
  Out(0xa0, 0x11);
  Out(0x21, kVectorBase);  // ICW2: vector bases
  Out(0xa1, kVectorBase + 8);
  Out(0x21, 0x04);  // ICW3: slave on IRQ 2
  Out(0xa1, 0x02);
  Out(0x21, 0x01);  // ICW4: 8086 mode
  Out(0xa1, 0x01);
  Out(0x21, 0xff);  // mask everything
  Out(0xa1, 0xff);
}

}  // namespace pic
