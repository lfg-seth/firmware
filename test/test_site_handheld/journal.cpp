#include "../../src/modules/SiteStore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

using namespace site;
class Flash : public Media {
public:
  std::vector<uint8_t> bytes = std::vector<uint8_t>(Sector * Slots, 0xff);
  Reservation saved{0, NoSlot, 0, 0, 0};
  unsigned failAfter = UINT32_MAX;
  Flash() { seal(saved); }
  bool read(unsigned a, void *dest, size_t n) override {
    memcpy(dest, bytes.data() + a, n);
    return true;
  }
  bool write(unsigned a, const void *src, size_t n) override {
    auto b = static_cast<const uint8_t *>(src);
    for (size_t i = 0; i < n; i++) {
      if (i == failAfter)
        return false;
      assert((bytes[a + i] & b[i]) == b[i]);
      bytes[a + i] &= b[i];
    }
    return true;
  }
  bool erase() override {
    std::fill(bytes.begin(), bytes.end(), 0xff);
    return true;
  }
  bool get(Reservation &r) override {
    r = saved;
    return true;
  }
  bool put(const Reservation &r) override {
    saved = r;
    return true;
  }
};
int main() {
  for (uint16_t mask = 1; mask <= All; ++mask) {
    uint32_t found = 0;
    for (unsigned j = 0; j < count(mask); ++j) {
      unsigned index = targetAt(mask, j);
      assert((mask & (1U << index)) && !(found & (1U << index)));
      found |= 1U << index;
      for (unsigned round = 1; round < Rounds; ++round)
        assert(targetAt(mask, j + round * count(mask)) == index);
    }
    assert(found == mask);
  }
  Flash media;
  Store journal(media);
  assert(journal.init() && journal.freeSlot() == 0);
  uint16_t pair = (1U << 2) | (1U << 5);
  for (unsigned cut = 0; cut < sizeof(Header); ++cut) {
    Flash broken;
    Store first(broken);
    assert(first.init());
    broken.failAfter = cut;
    assert(!first.start(0, pair));
    broken.failAfter = UINT32_MAX;
    Store recovered(broken);
    Header h{};
    Finish f{};
    assert(recovered.init() && recovered.counter() == 1 &&
           recovered.header(0, h) && validHeader(h));
    assert(recovered.footer(0, f) && status(f) == Status::Interrupted &&
           recovered.freeSlot() == 1);
  }
  assert(journal.start(0, pair) && journal.beginAttempt(0, 0, 100));
  Result reply{};
  reply.outcome = uint8_t(Outcome::Reply);
  reply.valid = UpSnr | DownSnr;
  reply.upQuarter = -39;
  reply.downQuarter = 0;
  assert(journal.endAttempt(0, 0, reply));
  assert(journal.beginAttempt(0, 1, 3000));
  Result missed{};
  missed.outcome = uint8_t(Outcome::Timeout);
  missed.elapsed = 7501;
  assert(journal.endAttempt(0, 1, missed) && journal.beginAttempt(0, 2, 7600));
  for (unsigned cut = 0; cut < sizeof(Result); ++cut) {
    Flash broken = media;
    Store run(broken);
    assert(run.init());
    broken.failAfter = cut;
    assert(!run.endAttempt(0, 2, reply));
    Store next(broken);
    Cell c{};
    Finish f{};
    assert(next.init() && next.footer(0, f) &&
           status(f) == Status::Interrupted);
    assert(next.cell(0, 0, c) && outcome(c, 0) == Outcome::Reply);
    assert(next.cell(0, 1, c) && outcome(c, 1) == Outcome::Timeout);
    assert(next.cell(0, 2, c) && outcome(c, 2) == Outcome::Unknown);
  }
  Link link{};
  link.add(Outcome::Timeout, missed);
  link.add(Outcome::Unknown, reply);
  link.add(Outcome::Reply, reply);
  reply.upQuarter = -40;
  reply.valid = UpSnr;
  link.add(Outcome::Reply, reply);
  assert(link.attempts == 4 && link.replies == 2 && link.nu == 2 &&
         link.nd == 1);
  assert(median8(link.up, link.nu) == -79 && median8(link.down, link.nd) == 0);
  assert(journal.finish(0, Status::Manual, 8200));
  Finish f{};
  assert(journal.footer(0, f) && status(f) == Status::Manual);
  for (unsigned slot = 1; slot < Slots; ++slot) {
    assert(journal.freeSlot() == int(slot) && journal.start(slot, All));
    for (unsigned i = 0; i < TargetCount * Rounds; ++i)
      assert(journal.beginAttempt(slot, i, i * 3000) &&
             journal.endAttempt(slot, i, missed));
    assert(journal.finish(slot, Status::Complete, 300000));
  }
  assert(journal.freeSlot() == -1 && journal.footer(0, f) &&
         status(f) == Status::Manual);
  assert(journal.counter() == Slots && journal.clear() &&
         journal.counter() == Slots);
  Store reboot(media);
  assert(reboot.init() && reboot.freeSlot() == 0 && reboot.start(0, pair) &&
         reboot.counter() == Slots + 1);
  puts("PASS all 1023 selections, round order, directional medians, torn "
       "header/outcome, interruption, manual/complete, capacity and counter "
       "across erase");
}
