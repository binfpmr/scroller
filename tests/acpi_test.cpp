#include "drivers/acpi.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

#include "test.h"

namespace {

// Builds ACPI tables in host memory; "physical addresses" are host pointers.
class TableBuilder {
 public:
  // A table with a standard header and `body`; the checksum is filled in.
  std::vector<uint8_t>& Table(const char* signature, const std::vector<uint8_t>& body) {
    tables_.emplace_back(36 + body.size(), 0);
    std::vector<uint8_t>& t = tables_.back();
    std::memcpy(t.data(), signature, 4);
    Put32(t, 4, static_cast<uint32_t>(t.size()));
    t[8] = 1;
    std::memcpy(t.data() + 36, body.data(), body.size());
    Fix(t, 9, t.size());
    return t;
  }
  static void Put32(std::vector<uint8_t>& t, size_t at, uint32_t v) { std::memcpy(t.data() + at, &v, 4); }
  static void Put64(std::vector<uint8_t>& t, size_t at, uint64_t v) { std::memcpy(t.data() + at, &v, 8); }
  static void Fix(std::vector<uint8_t>& t, size_t checksum_at, size_t length) {
    t[checksum_at] = 0;
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
      sum = static_cast<uint8_t>(sum + t[i]);
    }
    t[checksum_at] = static_cast<uint8_t>(-sum);
  }

 private:
  std::deque<std::vector<uint8_t>> tables_;  // deque: references to earlier tables stay valid
};

std::vector<uint8_t> MadtBody() {
  std::vector<uint8_t> b = {0x00, 0x00, 0xe0, 0xfe, 0x01, 0x00, 0x00, 0x00};  // LAPIC 0xFEE00000, PCAT_COMPAT
  const std::vector<uint8_t> entries = {
      0, 8,  0, 0, 1,    0,    0,    0,                    // CPU 0 enabled
      0, 8,  1, 1, 0,    0,    0,    0,                    // CPU 1 disabled
      1, 12, 7, 0, 0x00, 0x00, 0xc0, 0xfe, 0,    0, 0, 0,  // IOAPIC id 7 at 0xFEC00000, GSI base 0
      2, 10, 0, 0, 2,    0,    0,    0,    0,    0,        // ISA IRQ 0 -> GSI 2
      2, 10, 0, 9, 9,    0,    0,    0,    0x0d, 0,        // ISA IRQ 9 -> GSI 9, active low, level
  };
  b.insert(b.end(), entries.begin(), entries.end());
  return b;
}

}  // namespace

TEST(FindsMadtThroughXsdtAndParsesIt) {
  TableBuilder tb;
  std::vector<uint8_t>& madt = tb.Table("APIC", MadtBody());
  std::vector<uint8_t>& facp = tb.Table("FACP", std::vector<uint8_t>(16, 0));
  std::vector<uint8_t> xsdt_body(16);
  const uint64_t facp_address = reinterpret_cast<uint64_t>(facp.data());
  const uint64_t madt_address = reinterpret_cast<uint64_t>(madt.data());
  std::memcpy(xsdt_body.data(), &facp_address, 8);
  std::memcpy(xsdt_body.data() + 8, &madt_address, 8);
  std::vector<uint8_t>& xsdt = tb.Table("XSDT", xsdt_body);

  std::vector<uint8_t> rsdp(36, 0);
  std::memcpy(rsdp.data(), "RSD PTR ", 8);
  rsdp[15] = 2;
  TableBuilder::Put32(rsdp, 20, 36);
  TableBuilder::Put64(rsdp, 24, reinterpret_cast<uint64_t>(xsdt.data()));
  TableBuilder::Fix(rsdp, 8, 20);
  TableBuilder::Fix(rsdp, 32, 36);

  const uint8_t* found = acpi::FindTable(rsdp.data(), "APIC");
  EXPECT_TRUE(found == madt.data());
  EXPECT_TRUE(acpi::FindTable(rsdp.data(), "HPET") == nullptr);

  acpi::Madt m;
  EXPECT_TRUE(acpi::ParseMadt(found, &m));
  EXPECT_EQ(m.local_apic_address, 0xfee00000u);
  EXPECT_EQ(m.cpu_count, 1);
  EXPECT_TRUE(m.has_8259);
  EXPECT_EQ(m.io_apic_count, 1);
  EXPECT_EQ(m.io_apics[0].id, 7);
  EXPECT_EQ(m.io_apics[0].address, 0xfec00000u);
  uint16_t flags = 0xffff;
  EXPECT_EQ(m.GsiForIsaIrq(0, &flags), 2u);
  EXPECT_EQ(flags, 0);
  EXPECT_EQ(m.GsiForIsaIrq(1, &flags), 1u);
  EXPECT_EQ(m.GsiForIsaIrq(9, &flags), 9u);
  EXPECT_EQ(flags, 0x0d);
}

TEST(RejectsBadChecksumsAndSignatures) {
  TableBuilder tb;
  std::vector<uint8_t>& madt = tb.Table("APIC", MadtBody());
  std::vector<uint8_t> xsdt_body(8);
  const uint64_t madt_address = reinterpret_cast<uint64_t>(madt.data());
  std::memcpy(xsdt_body.data(), &madt_address, 8);
  std::vector<uint8_t>& xsdt = tb.Table("XSDT", xsdt_body);
  std::vector<uint8_t> rsdp(36, 0);
  std::memcpy(rsdp.data(), "RSD PTR ", 8);
  rsdp[15] = 2;
  TableBuilder::Put32(rsdp, 20, 36);
  TableBuilder::Put64(rsdp, 24, reinterpret_cast<uint64_t>(xsdt.data()));
  TableBuilder::Fix(rsdp, 8, 20);
  TableBuilder::Fix(rsdp, 32, 36);
  EXPECT_TRUE(acpi::FindTable(rsdp.data(), "APIC") == madt.data());

  madt[40] ^= 1;  // the MADT's checksum no longer matches
  EXPECT_TRUE(acpi::FindTable(rsdp.data(), "APIC") == nullptr);
  madt[40] ^= 1;
  rsdp[9] ^= 1;  // RSDP checksum broken
  EXPECT_TRUE(acpi::FindTable(rsdp.data(), "APIC") == nullptr);
  std::vector<uint8_t> not_rsdp(36, 0);
  EXPECT_TRUE(acpi::FindTable(not_rsdp.data(), "APIC") == nullptr);
  EXPECT_TRUE(acpi::FindTable(nullptr, "APIC") == nullptr);
}

TEST(RejectsMalformedMadtEntries) {
  TableBuilder tb;
  std::vector<uint8_t> body = MadtBody();
  body.push_back(1);
  body.push_back(0);  // entry with size 0: would loop forever
  std::vector<uint8_t>& madt = tb.Table("APIC", body);
  acpi::Madt m;
  EXPECT_TRUE(!acpi::ParseMadt(madt.data(), &m));
}
