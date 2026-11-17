#pragma once
#include "SiteJournal.h"

namespace site {
constexpr uint16_t NoSlot = 0xffff;
struct Reservation {
  uint32_t number;
  uint16_t slot, mask;
  uint32_t clearing, crc;
};
class Media {
public:
  virtual bool read(unsigned offset, void *buf, size_t n) = 0;
  virtual bool write(unsigned offset, const void *buf, size_t n) = 0;
  virtual bool erase() = 0;
  virtual bool get(Reservation &meta) = 0;
  virtual bool put(const Reservation &meta) = 0;
};
class Store {
  Media &media;
  Reservation meta{};
  bool commitMeta(Reservation next) {
    seal(next);
    if (!media.put(next))
      return false;
    meta = next;
    return true;
  }
  template <class T> bool writeRecord(unsigned offset, T record) {
    seal(record);
    T checked{};
    return media.write(offset, &record, sizeof record) &&
           media.read(offset, &checked, sizeof checked) &&
           !memcmp(&checked, &record, sizeof record);
  }
  bool writeHeader() {
    Header h{Magic, meta.number, meta.mask, 1, 0};
    return writeRecord(meta.slot * Sector, h);
  }
  bool finishErase() {
    if (!media.erase())
      return false;
    return commitMeta({meta.number, NoSlot, 0, 0, 0});
  }

public:
  explicit Store(Media &m) : media(m) {}
  uint32_t counter() const { return meta.number; }
  bool init() {
    if (!media.get(meta) || !valid(meta))
      return false;
    if (meta.clearing)
      return finishErase();
    if (meta.slot == NoSlot)
      return true;
    if (meta.slot >= Slots || !meta.number || !meta.mask || (meta.mask & ~All))
      return false;
    Header h{};
    if (!header(meta.slot, h))
      return false;
    if (validHeader(h))
      return h.number == meta.number && h.mask == meta.mask;
    // A reset during the header program can leave a subset of its intended zero
    // bits.
    return writeHeader();
  }
  bool header(unsigned slot, Header &h) {
    return slot < Slots && media.read(slot * Sector, &h, sizeof h);
  }
  bool footer(unsigned slot, Finish &f) {
    return slot < Slots &&
           media.read(slot * Sector + offsetof(SectorData, finish), &f,
                      sizeof f);
  }
  bool cell(unsigned slot, unsigned i, Cell &c) {
    return slot < Slots && i < Rounds * TargetCount &&
           media.read(slot * Sector + cellOffset(i), &c, sizeof c);
  }
  int freeSlot() {
    for (unsigned slot = 0; slot < Slots; ++slot) {
      Header h{};
      if (!header(slot, h))
        return -2;
      if (!blank(&h, sizeof h))
        continue;
      // Reserve only a wholly erased sector. It fits a complete ten-target
      // test.
      uint8_t b[128];
      bool erased = true;
      for (unsigned off = 0; off < Sector && erased; off += sizeof b) {
        if (!media.read(slot * Sector + off, b, sizeof b))
          return -2;
        erased = blank(b, sizeof b);
      }
      if (erased)
        return int(slot);
    }
    return -1;
  }
  bool start(unsigned slot, uint16_t mask) {
    if (slot >= Slots || !mask || (mask & ~All) || meta.number == UINT32_MAX)
      return false;
    if (!commitMeta({meta.number + 1, uint16_t(slot), mask, 0, 0}))
      return false;
    return writeHeader();
  }
  bool beginAttempt(unsigned slot, unsigned i, uint32_t elapsed) {
    return i < Rounds * TargetCount &&
           writeRecord(slot * Sector + cellOffset(i),
                       Pending{i + 1, elapsed, 0});
  }
  bool endAttempt(unsigned slot, unsigned i, Result r) {
    return i < Rounds * TargetCount &&
           writeRecord(slot * Sector + cellOffset(i) + offsetof(Cell, result),
                       r);
  }
  bool finish(unsigned slot, Status why, uint32_t elapsed) {
    return writeRecord(slot * Sector + offsetof(SectorData, finish),
                       Finish{uint32_t(why), elapsed, 0});
  }
  bool clear() {
    return commitMeta({meta.number, NoSlot, 0, 1, 0}) && finishErase();
  }
};
} // namespace site
