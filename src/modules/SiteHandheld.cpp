#include "SiteHandheld.h"
#ifdef SNORR_SITE_HANDHELD
#include "RTC.h"
#include "SiteStore.h"
#include <Arduino.h>
#include <OLEDDisplay.h>
#include <Preferences.h>
#include <esp_attr.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <hal/gpio_ll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

namespace site {
namespace {
Preferences prefs;
struct ButtonEdge {
  uint32_t time;
  bool down;
};
StaticQueue_t edgeQueueStorage;
uint8_t edgeData[32 * sizeof(ButtonEdge)];
QueueHandle_t edgeQueue = nullptr;
volatile bool lostEdges = false;
bool rawButton = false, stableButton = false, heldButton = false,
     waitingTap = false;
uint32_t rawSince = 0, pressedAt = 0, lastRelease = 0;
class Flash : public Media {
public:
  const esp_partition_t *p = nullptr;
  bool read(unsigned off, void *b, size_t n) override {
    return p && esp_partition_read(p, off, b, n) == ESP_OK;
  }
  bool write(unsigned off, const void *b, size_t n) override {
    return p && esp_partition_write(p, off, b, n) == ESP_OK;
  }
  bool erase() override {
    return p && esp_partition_erase_range(p, 0, Slots * Sector) == ESP_OK;
  }
  bool get(Reservation &r) override {
    if (prefs.isKey("reserve"))
      return prefs.getBytes("reserve", &r, sizeof r) == sizeof r;
    // Recover the high water mark from headers if preferences were never
    // initialized.
    r = {0, NoSlot, 0, 0, 0};
    for (unsigned i = 0; i < Slots; ++i) {
      Header h{};
      if (!read(i * Sector, &h, sizeof h))
        return false;
      if (validHeader(h) && h.number > r.number)
        r.number = h.number;
    }
    seal(r);
    return put(r);
  }
  bool put(const Reservation &r) override {
    Reservation checked{};
    return prefs.putBytes("reserve", &r, sizeof r) == sizeof r &&
           prefs.getBytes("reserve", &checked, sizeof checked) ==
               sizeof checked &&
           !memcmp(&r, &checked, sizeof r);
  }
} flash;
Store store(flash);
constexpr uint32_t TimeMagic = 0x5349544d;
RTC_DATA_ATTR uint32_t clockMarker = 0;
RTC_DATA_ATTR uint32_t syncEpoch = 0;
bool ready = false, storage = false, sweeping = false, manual = false;
enum class View {
  Home,
  One,
  Several,
  Walk,
  Test,
  Result,
  History,
  Detail,
  Message
};
View view = View::Home, returnTo = View::Home;
constexpr uint16_t defaultPair = (1U << 2) | (1U << 5);
uint16_t selected = defaultPair, editing = 0, displayed = 0;
unsigned choice = 0, page = 0, activeSlot = Slots, savedCount = 0,
         liveCount = 0;
uint32_t began = 0, siteNumber = 0, endElapsed = 0, pageSince = 0;
Status displayedStatus = Status::Interrupted;
Link links[TargetCount]{};
uint16_t historySlots[Slots]{};
unsigned historySize = 0, historyIndex = 0;
const char *notice = "";
const char *homeChoices[] = {"Walk last targets", "One repeater",
                             "Several repeaters", "All 10 repeaters",
                             "History"};
const char *postChoices[] = {"Test again", "Next pair", "Walking", "Main menu"};
void enter(View next) {
  view = next;
  choice = page = 0;
  pageSince = millis();
}
void message(const char *text, View back) {
  notice = text;
  returnTo = back;
  enter(View::Message);
}
void clearReadings(uint16_t mask) {
  for (auto &link : links)
    link = Link{};
  displayed = mask;
  liveCount = 0;
  page = 0;
}
void walk() {
  clearReadings(selected);
  enter(View::Walk);
}
void choose(uint16_t mask) {
  if (!mask) {
    message("Check a repeater", View::Several);
    return;
  }
  if (mask != selected && prefs.putUShort("selected", mask) != sizeof(mask)) {
    message("Selection save failed", View::Home);
    return;
  }
  selected = mask;
  walk();
}
bool summary(unsigned slot) {
  Header h{};
  Finish f{};
  if (!store.header(slot, h) || !validHeader(h) || !store.footer(slot, f))
    return false;
  clearReadings(h.mask);
  siteNumber = h.number;
  displayedStatus = status(f);
  savedCount = 0;
  endElapsed = valid(f) ? f.elapsed : 0;
  for (unsigned i = 0; i < count(h.mask) * Rounds; ++i) {
    Cell c{};
    if (!store.cell(slot, i, c))
      return false;
    if (blank(&c.pending, sizeof c.pending))
      break;
    ++savedCount;
    links[targetAt(h.mask, i)].add(outcome(c, i), c.result);
    if (!valid(f)) {
      if (valid(c.result))
        endElapsed = c.result.elapsed;
      else if (valid(c.pending))
        endElapsed = c.pending.elapsed;
    }
  }
  return true;
}
void loadHistory() {
  historySize = historyIndex = 0;
  if (!storage) {
    message("Journal unavailable", View::Home);
    return;
  }
  // Slots are allocated in ascending order between explicit clears.
  for (unsigned i = Slots; i > 0; --i) {
    Header h{};
    if (!store.header(i - 1, h)) {
      message("Flash read failed", View::Home);
      return;
    }
    if (validHeader(h))
      historySlots[historySize++] = i - 1;
  }
  if (!historySize) {
    message("No sites yet", View::Home);
    return;
  }
  enter(View::History);
}
void start(bool saved) {
  if (sweeping) {
    if (!saved)
      return;
    survey::cancelWalk();
    sweeping = false;
  }
  if (!clockTime()) {
    message("Sync USB or Wi-Fi", View::Walk);
    return;
  }
  if (!survey::fieldReady(selected)) {
    message("Check profile / peers", View::Walk);
    return;
  }
  if (saved) {
    int slot = storage ? store.freeSlot() : -2;
    if (slot < 0) {
      message(slot == -1 ? "Full: USB export/clear" : "Journal unavailable",
              View::Walk);
      return;
    }
    if (!store.start(slot, selected)) {
      storage = false;
      message("Flash write failed", View::Walk);
      return;
    }
    activeSlot = slot;
    siteNumber = store.counter();
    savedCount = 0;
    manual = false;
  }
  clearReadings(selected);
  began = millis();
  endElapsed = 0;
  enter(saved ? View::Test : View::Walk);
  sweeping = !saved;
  if (!survey::fieldStart(selected, saved)) {
    sweeping = false;
    message("Radio start failed", View::Walk);
  }
}
void tap(bool twice) {
  if (twice && view == View::Walk) {
    survey::cancelWalk();
    sweeping = false;
    enter(View::Home);
    return;
  }
  switch (view) {
  case View::Home:
    choice = (choice + 1) % 5;
    break;
  case View::One:
    choice = (choice + 1) % (TargetCount + 1);
    break;
  case View::Several:
    choice = (choice + 1) % (TargetCount + 2);
    break;
  case View::Walk:
    if (!sweeping)
      start(false);
    else {
      ++page;
      pageSince = millis();
    }
    break;
  case View::Test:
  case View::Detail:
    ++page;
    pageSince = millis();
    break;
  case View::Result:
    choice = (choice + 1) % 4;
    break;
  case View::History:
    historyIndex = (historyIndex + 1) % (historySize + 1);
    break;
  case View::Message:
    enter(returnTo);
    break;
  }
}
void hold() {
  switch (view) {
  case View::Home:
    if (choice == 0)
      walk();
    else if (choice == 1)
      enter(View::One);
    else if (choice == 2) {
      editing = selected;
      enter(View::Several);
    } else if (choice == 3)
      choose(All);
    else
      loadHistory();
    break;
  case View::One:
    if (choice == TargetCount)
      enter(View::Home);
    else
      choose(1U << choice);
    break;
  case View::Several:
    if (choice == TargetCount)
      choose(editing);
    else if (choice > TargetCount)
      enter(View::Home);
    else
      editing ^= 1U << choice;
    break;
  case View::Walk:
    start(true);
    break;
  case View::Test:
    manual = true;
    survey::fieldFinish();
    break;
  case View::Result:
    if (choice == 0)
      start(true);
    else if (choice == 1) {
      ++page;
      pageSince = millis();
    } else if (choice == 2)
      walk();
    else
      enter(View::Home);
    break;
  case View::History:
    if (historyIndex == historySize)
      enter(View::Home);
    else if (summary(historySlots[historyIndex]))
      enter(View::Detail);
    else
      message("Flash read failed", View::Home);
    break;
  case View::Detail:
    enter(View::History);
    break;
  case View::Message:
    enter(returnTo);
    break;
  }
}
} // namespace
uint32_t clockTime() {
  auto ntp = getValidTime(RTCQualityNTP);
  if (ntp >= 1700000000) {
    if (clockMarker != TimeMagic || !syncEpoch)
      syncEpoch = ntp;
    clockMarker = TimeMagic;
    return ntp;
  }
  if (clockMarker != TimeMagic || !syncEpoch)
    return 0;
  struct timeval tv {};
  gettimeofday(&tv, nullptr);
  return tv.tv_sec >= syncEpoch ? uint32_t(tv.tv_sec) : 0;
}
bool setUsbTime(uint32_t t) {
  if (t < 1700000000 || sweeping || view == View::Test)
    return false;
  struct timeval tv {
    time_t(t), 0
  };
  if (settimeofday(&tv, nullptr))
    return false;
  syncEpoch = t;
  clockMarker = TimeMagic;
  return true;
}
void setup() {
  edgeQueue =
      xQueueCreateStatic(32, sizeof(ButtonEdge), edgeData, &edgeQueueStorage);
  bool opened = prefs.begin("sitesurvey", false);
  flash.p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                     esp_partition_subtype_t(0x40), "sites");
  storage =
      opened && flash.p && flash.p->size == Slots * Sector && store.init();
  if (opened)
    selected = prefs.getUShort("selected", defaultPair);
  if (!selected || (selected & ~All))
    selected = defaultPair;
  ready = true;
}
void IRAM_ATTR buttonEdge() {
  if (!edgeQueue)
    return;
  ButtonEdge event{uint32_t(millis()),
                   gpio_ll_get_level(&GPIO, gpio_num_t(BUTTON_PIN)) == 0};
  BaseType_t wake = pdFALSE;
  if (xQueueSendFromISR(edgeQueue, &event, &wake) != pdTRUE)
    lostEdges = true;
  if (wake)
    portYIELD_FROM_ISR();
}
static void advanceButton(uint32_t now) {
  if (rawButton != stableButton && now - rawSince >= 35) {
    stableButton = rawButton;
    if (stableButton) {
      pressedAt = rawSince;
      heldButton = false;
    } else if (!heldButton) {
      if (view == View::Walk && waitingTap && rawSince - lastRelease < 330) {
        waitingTap = false;
        tap(true);
      } else if (view == View::Walk) {
        waitingTap = true;
        lastRelease = rawSince;
      } else
        tap(false);
    }
  }
  if (rawButton && stableButton && !heldButton && now - pressedAt >= 850) {
    heldButton = true;
    waitingTap = false;
    hold();
  }
  if (waitingTap && !stableButton && now - lastRelease >= 330) {
    waitingTap = false;
    tap(false);
  }
}
void button() {
  if (!ready || !edgeQueue)
    return;
  if (lostEdges) {
    lostEdges = false;
    xQueueReset(edgeQueue);
    rawButton = stableButton =
        gpio_ll_get_level(&GPIO, gpio_num_t(BUTTON_PIN)) == 0;
    heldButton = true;
    waitingTap = false;
    rawSince = millis();
    return;
  }
  ButtonEdge e;
  while (xQueueReceive(edgeQueue, &e, 0) == pdTRUE) {
    advanceButton(e.time);
    if (e.down != rawButton) {
      rawButton = e.down;
      rawSince = e.time;
    }
  }
  advanceButton(millis());
}
bool pending(unsigned ordinal, uint32_t target, uint32_t now) {
  if (!ordinal || ordinal > count(displayed) * (sweeping ? 1 : Rounds))
    return false;
  auto i = targetAt(displayed, ordinal - 1);
  if (i >= TargetCount || targets[i].id != target)
    return false;
  if (!sweeping && (view != View::Test || !storage ||
                    !store.beginAttempt(activeSlot, ordinal - 1, now - began)))
    return false;
  ++links[i].attempts;
  if (!sweeping)
    savedCount = ordinal;
  return true;
}
bool result(unsigned ordinal, uint32_t now, Result r) {
  if (!ordinal || !displayed)
    return false;
  unsigned i = targetAt(displayed, ordinal - 1);
  r.elapsed = now - began;
  if (!sweeping && !store.endAttempt(activeSlot, ordinal - 1, r))
    return false;
  --links[i].attempts;
  links[i].add(Outcome(r.outcome), r);
  liveCount = ordinal;
  endElapsed = r.elapsed;
  return true;
}
void stopped(bool early) {
  if (sweeping) {
    sweeping = false;
    return;
  }
  displayedStatus = early ? Status::Manual : Status::Complete;
  endElapsed = millis() - began;
  if (!store.finish(activeSlot, displayedStatus, endElapsed)) {
    fault("Flash write failed");
    return;
  }
  enter(View::Result);
}
void fault(const char *text) {
  storage = false;
  sweeping = false;
  message(text, View::Home);
}

namespace {
struct Frame {
  char title[35]{}, hint[34]{}, line[4][34]{}, col[4][2][12]{};
  bool table = false;
};
void number(char *out, size_t cap, int eighths) {
  unsigned mag = abs(eighths);
  snprintf(out, cap, "%s%u.%03u", eighths < 0 ? "-" : "", mag / 8,
           (mag % 8) * 125);
  char *end = out + strlen(out) - 1;
  while (end > out && *end == '0')
    *end-- = 0;
  if (*end == '.')
    *end = 0;
}
void table(Frame &f) {
  f.table = true;
  unsigned n = count(displayed), pages = (n + 1) / 2;
  if (!pages)
    return;
  if (pages > 1 && millis() - pageSince > 4500) {
    ++page;
    pageSince = millis();
  }
  page %= pages;
  if (pages > 1) {
    size_t used = strlen(f.title);
    snprintf(f.title + used, sizeof(f.title) - used, " %u/%u", page + 1, pages);
  }
  strcpy(f.line[1], "Replies");
  strcpy(f.line[2], "Up dB");
  strcpy(f.line[3], "Dn dB");
  for (unsigned j = 0; j < 2 && page * 2 + j < n; ++j) {
    auto idx = targetAt(displayed, page * 2 + j);
    auto &s = links[idx];
    strcpy(f.col[0][j], targets[idx].label);
    snprintf(f.col[1][j], sizeof(f.col[1][j]), "%u/%u", s.replies, s.attempts);
    if (s.nu)
      number(f.col[2][j], sizeof(f.col[2][j]), median8(s.up, s.nu));
    else
      strcpy(f.col[2][j], "-");
    if (s.nd)
      number(f.col[3][j], sizeof(f.col[3][j]), median8(s.down, s.nd));
    else
      strcpy(f.col[3][j], "-");
  }
}
void frame(Frame &f) {
  if (!ready) {
    strcpy(f.title, "Starting survey...");
    return;
  }
  switch (view) {
  case View::Home:
    strcpy(f.title, "Site survey");
    snprintf(f.line[0], 34, "> %s", homeChoices[choice]);
    snprintf(f.line[1], 34, "%u selected", count(selected));
    strcpy(f.line[2], clockTime() ? "Clock ready" : "Sync USB or Wi-Fi");
    strcpy(f.hint, "Tap next  Hold choose");
    break;
  case View::One:
  case View::Several: {
    bool several = view == View::Several;
    strcpy(f.title, several ? "Several repeaters" : "One repeater");
    if (choice < TargetCount) {
      snprintf(f.line[0], 34, "> %s%s",
               several ? ((editing & (1U << choice)) ? "[x] " : "[ ] ") : "",
               targets[choice].label);
      if (several) {
        snprintf(f.line[1], 34, "%u checked", count(editing));
        strcpy(f.line[2], "Done follows R011");
      }
    } else
      strcpy(f.line[0], several && choice == TargetCount ? "> Done" : "> Back");
    strcpy(f.hint, several && choice < TargetCount ? "Tap next  Hold check"
                                                   : "Tap next  Hold choose");
    break;
  }
  case View::Walk:
    strcpy(f.title, sweeping ? "Sweep in progress" : "Walk  2 taps: menu");
    table(f);
    strcpy(f.hint, "Tap sweep  Hold site");
    break;
  case View::Test:
  case View::Result:
  case View::Detail:
    snprintf(f.title, sizeof f.title, "#%lu %s", (unsigned long)siteNumber,
             view == View::Test ? (manual ? "finishing" : "testing")
                                : name(displayedStatus));
    table(f);
    if (view == View::Test)
      strcpy(f.hint, "Tap page  Hold finish");
    else if (view == View::Result)
      snprintf(f.hint, sizeof f.hint, "Hold: %s  Tap >", postChoices[choice]);
    else
      strcpy(f.hint, "Tap page  Hold back");
    break;
  case View::History: {
    strcpy(f.title, "History  newest first");
    if (historyIndex == historySize)
      strcpy(f.line[0], "> Back");
    else {
      Header h{};
      Finish fin{};
      if (store.header(historySlots[historyIndex], h) &&
          store.footer(historySlots[historyIndex], fin))
        snprintf(f.line[0], 34, "> #%lu  %s", (unsigned long)h.number,
                 name(status(fin)));
    }
    strcpy(f.hint, "Tap next  Hold view");
    break;
  }
  case View::Message:
    strcpy(f.title, "Site survey");
    snprintf(f.line[0], 34, "%s", notice);
    if (!strncmp(notice, "Full:", 5))
      strcpy(f.line[2], "Walking still available");
    strcpy(f.hint, "Tap to return");
    break;
  }
}
void printRow(char *buf, size_t cap, const Header &h, Status statusCode,
              unsigned i, const Cell &c) {
  unsigned target = targetAt(h.mask, i);
  Outcome kind = outcome(c, i);
  const auto &r = c.result;
  char start[14] = "", end[14] = "", up[8] = "", down[8] = "", us[12] = "",
       ds[12] = "", rtt[14] = "";
  if (valid(c.pending) && c.pending.ordinal == i + 1)
    snprintf(start, sizeof start, "%lu", (unsigned long)c.pending.elapsed);
  if (kind != Outcome::Unknown)
    snprintf(end, sizeof end, "%lu", (unsigned long)r.elapsed);
  if (kind == Outcome::Reply) {
    if (r.valid & UpRssi)
      snprintf(up, sizeof up, "%d", r.upRssi);
    if (r.valid & DownRssi)
      snprintf(down, sizeof down, "%d", r.downRssi);
    if (r.valid & UpSnr)
      number(us, sizeof us, 2 * r.upQuarter);
    if (r.valid & DownSnr)
      number(ds, sizeof ds, 2 * r.downQuarter);
    if (r.valid & Rtt)
      snprintf(rtt, sizeof rtt, "%lu", (unsigned long)r.rtt);
  }
  snprintf(buf, cap, "ROW %lu,%s,%u,%u,%s,!%08lx,%s,%s,%s,%s,%s,%s,%s,%s",
           (unsigned long)h.number, name(statusCode), i / count(h.mask) + 1,
           i + 1, targets[target].label, (unsigned long)targets[target].id,
           start, end, name(kind), up, us, down, ds, rtt);
}
} // namespace
void draw(OLEDDisplay *d) {
  Frame f{};
  frame(f);
  d->clear();
  d->setTextAlignment(TEXT_ALIGN_LEFT);
  d->setFont(ArialMT_Plain_10);
  d->drawString(0, 0, f.title);
  for (unsigned i = 0; i < 4; ++i) {
    int y = 11 + 10 * i;
    d->drawString(0, y, f.line[i]);
    if (f.table) {
      d->drawString(42, y, f.col[i][0]);
      d->drawString(85, y, f.col[i][1]);
    }
  }
  d->drawString(0, 52, f.hint);
  d->display();
}
unsigned usb(const uint8_t *bytes, size_t n, uint8_t *reply, size_t cap) {
  if (n < 5 || n > 80 || memcmp(bytes, "\0SH1", 4) || cap < 256)
    return 0;
  char cmd[80]{};
  memcpy(cmd, bytes + 4, n - 4);
  memcpy(reply, "\0SH1", 4);
  char *out = (char *)reply + 4;
  cap -= 4;
  strcpy(out, "ERR unknown command");
  unsigned slot = 0, idx = 0;
  unsigned long value = 0;
  char extra = 0;
  if (!strcmp(cmd, "INFO")) {
    snprintf(out, cap, "INFO 1 %lu %u %u %u %u %u %lu %u",
             (unsigned long)store.counter(), Slots, selected, unsigned(view),
             savedCount, liveCount, (unsigned long)clockTime(), storage);
  } else if (!strcmp(cmd, "UI")) {
    Frame f{};
    frame(f);
    snprintf(out, cap, "UI %s|%s,%s,%s|%s,%s,%s|%s,%s,%s|%s,%s,%s|%s", f.title,
             f.line[0], f.col[0][0], f.col[0][1], f.line[1], f.col[1][0],
             f.col[1][1], f.line[2], f.col[2][0], f.col[2][1], f.line[3],
             f.col[3][0], f.col[3][1], f.hint);
  } else if (sscanf(cmd, "CLOCK %lu %c", &value, &extra) == 1) {
    strcpy(out, setUsbTime(value) ? "OK" : "ERR clock");
  } else if (!storage || view == View::Test || sweeping) {
    strcpy(out, "ERR busy or storage unavailable");
  } else if (sscanf(cmd, "NEXT %u %c", &slot, &extra) == 1 && slot <= Slots) {
    strcpy(out, "END");
    for (; slot < Slots; ++slot) {
      Header h{};
      Finish f{};
      if (!store.header(slot, h) || !store.footer(slot, f)) {
        strcpy(out, "ERR read");
        break;
      }
      if (validHeader(h)) {
        snprintf(out, cap, "NEXT %u %lu %s %u %lu", slot,
                 (unsigned long)h.number, name(status(f)), h.mask,
                 (unsigned long)(valid(f) ? f.elapsed : 0));
        break;
      }
    }
  } else if (sscanf(cmd, "SITE %u %c", &slot, &extra) == 1 && slot < Slots) {
    Header h{};
    Finish f{};
    if (!store.header(slot, h) || !store.footer(slot, f))
      strcpy(out, "ERR read");
    else if (!validHeader(h))
      strcpy(out, "EMPTY");
    else
      snprintf(out, cap, "SITE %lu %s %u %lu", (unsigned long)h.number,
               name(status(f)), h.mask,
               (unsigned long)(valid(f) ? f.elapsed : 0));
  } else if (sscanf(cmd, "ROW %u %u %c", &slot, &idx, &extra) == 2 &&
             slot < Slots) {
    Header h{};
    Finish f{};
    Cell c{};
    if (!store.header(slot, h) || !validHeader(h) || !store.footer(slot, f))
      strcpy(out, "ERR read");
    else if (idx >= count(h.mask) * Rounds)
      strcpy(out, "END");
    else if (!store.cell(slot, idx, c))
      strcpy(out, "ERR read");
    else if (blank(&c.pending, sizeof c.pending))
      strcpy(out, "END");
    else
      printRow(out, cap, h, status(f), idx, c);
  } else if (sscanf(cmd, "ERASE SURVEYS %lu %c", &value, &extra) == 1 &&
             value == store.counter()) {
    storage = store.clear();
    if (storage) {
      enter(View::Home);
      strcpy(out, "OK");
    } else
      strcpy(out, "ERR erase");
  }
  return strlen(out) + 4;
}
} // namespace site
#endif
