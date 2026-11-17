#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace site {
constexpr unsigned TargetCount = 10, Rounds = 10, Slots = 128, Sector = 4096;
constexpr uint16_t All = (1U << TargetCount) - 1;
struct Target {
  const char *label;
  uint32_t id;
};
constexpr Target targets[] = {{"R001", 0xf95b5501}, {"R002", 0x05237743},
                              {"R004", 0x49ac6d7f}, {"R005", 0xa65601b3},
                              {"R006", 0x638b9515}, {"R007", 0xb456f760},
                              {"R008", 0x120b04fd}, {"R009", 0xda581e5f},
                              {"R010", 0x77fdb9f1}, {"R011", 0x8737c2c9}};
inline unsigned count(uint16_t mask) { return __builtin_popcount(mask & All); }
inline unsigned targetAt(uint16_t mask, unsigned sequence) {
  unsigned n = count(mask);
  if (!n)
    return TargetCount;
  sequence %= n;
  for (unsigned i = 0; i < TargetCount; ++i)
    if ((mask & (1U << i)) && sequence-- == 0)
      return i;
  return TargetCount;
}
enum class Outcome : uint8_t { Unknown, Reply, Timeout, Deferred, Rejected };
enum class Status : uint8_t { Interrupted, Complete, Manual };
inline const char *name(Outcome v) {
  switch (v) {
  case Outcome::Reply:
    return "reply";
  case Outcome::Timeout:
    return "timeout";
  case Outcome::Deferred:
    return "deferred";
  case Outcome::Rejected:
    return "rejected";
  default:
    return "unknown";
  }
}
inline const char *name(Status v) {
  return v == Status::Complete ? "complete"
         : v == Status::Manual ? "manual"
                               : "interrupted";
}
// Size and CRC boundaries are part of the flash format. Never rewrite a
// committed field.
struct Header {
  uint32_t magic, number;
  uint16_t mask, version;
  uint32_t crc;
};
struct Finish {
  uint32_t status, elapsed, crc;
};
struct Pending {
  uint32_t ordinal, elapsed, crc;
};
struct Result {
  uint32_t elapsed, rtt;
  int16_t upRssi, downRssi, upQuarter, downQuarter;
  uint8_t outcome, valid;
  uint16_t reserved;
  uint32_t crc;
};
enum : uint8_t { UpRssi = 1, DownRssi = 2, UpSnr = 4, DownSnr = 8, Rtt = 16 };
struct Cell {
  Pending pending;
  Result result;
};
struct SectorData {
  Header header;
  Finish finish;
  Cell cells[Rounds * TargetCount];
};
static_assert(sizeof(Header) == 16 && sizeof(Pending) == 12 &&
                  sizeof(Result) == 24 && sizeof(SectorData) <= Sector,
              "site flash format");
constexpr uint32_t Magic = 0x31544953;
inline uint32_t checksum(const void *ptr, size_t n) {
  auto p = static_cast<const uint8_t *>(ptr);
  uint32_t c = ~0U;
  while (n--) {
    c ^= *p++;
    for (unsigned b = 0; b < 8; b++)
      c = (c >> 1) ^ (0xedb88320U & (0U - (c & 1U)));
  }
  return ~c;
}
template <class T> inline void seal(T &r) {
  r.crc = checksum(&r, offsetof(T, crc));
}
template <class T> inline bool valid(const T &r) {
  return r.crc == checksum(&r, offsetof(T, crc));
}
inline bool validHeader(const Header &h) {
  return h.magic == Magic && h.version == 1 && h.number && h.mask &&
         !(h.mask & ~All) && valid(h);
}
inline bool blank(const void *bytes, size_t size) {
  const auto *p = static_cast<const uint8_t *>(bytes);
  while (size--)
    if (*p++ != 0xff)
      return false;
  return true;
}
inline Status status(const Finish &f) {
  if (!valid(f))
    return Status::Interrupted;
  if (f.status == uint32_t(Status::Complete))
    return Status::Complete;
  if (f.status == uint32_t(Status::Manual))
    return Status::Manual;
  return Status::Interrupted;
}
inline Outcome outcome(const Cell &c, unsigned index) {
  if (!valid(c.pending) || c.pending.ordinal != index + 1 || !valid(c.result) ||
      c.result.outcome > uint8_t(Outcome::Rejected))
    return Outcome::Unknown;
  return Outcome(c.result.outcome);
}
inline unsigned cellOffset(unsigned index) {
  return offsetof(SectorData, cells) + index * sizeof(Cell);
}
struct Link {
  unsigned attempts = 0, replies = 0, nu = 0, nd = 0;
  int16_t up[Rounds]{}, down[Rounds]{};
  void add(Outcome kind, const Result &r) {
    ++attempts;
    if (kind != Outcome::Reply)
      return;
    ++replies;
    if ((r.valid & UpSnr) && nu < Rounds)
      up[nu++] = r.upQuarter;
    if ((r.valid & DownSnr) && nd < Rounds)
      down[nd++] = r.downQuarter;
  }
};
// Median in eighth-dB units preserves even-sized half-quarter readings exactly.
inline int median8(const int16_t *samples, unsigned n) {
  int16_t sorted[Rounds]{};
  for (unsigned i = 0; i < n; ++i) {
    unsigned j = i;
    while (j && sorted[j - 1] > samples[i]) {
      sorted[j] = sorted[j - 1];
      --j;
    }
    sorted[j] = samples[i];
  }
  return n ? int(sorted[(n - 1) / 2]) + sorted[n / 2] : 0;
}
} // namespace site
