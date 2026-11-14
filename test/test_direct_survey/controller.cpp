#include "DirectSurvey.cpp"
using namespace survey;
Command make(Op op, uint32_t serial) {
  Command c{};
  memcpy(c.magic, "\0SV1", 4);
  c.boot = boot;
  c.serial = serial;
  c.op = op;
  return c;
}
void apply(const Command &c) {
  assert(command(reinterpret_cast<const uint8_t *>(&c), sizeof c));
}
meshtastic_MeshPacket incoming(Frame f, uint32_t id, uint32_t source,
                               bool authorized = true) {
  meshtastic_MeshPacket p{};
  p.from = source;
  p.id = id;
  p.survey_physical = 1;
  p.decoded.portnum = SURVEY_PORT;
  tag(enrollment.group, f, source, id, 1, f.group);
  if (authorized)
    tag(pairKeys[0], f, source, id, 2, f.peer);
  memcpy(p.decoded.payload.bytes, &f, sizeof f);
  p.decoded.payload.size = sizeof f;
  return p;
}
int main() {
  setup();
  auto c = make(Enroll, 1);
  c.args[0] = 200;
  memset(c.keys, 8, 64);
  apply(c);
  assert(generation == 1);
  auto clock = make(Clock, 2);
  clock.args[0] = 1800000000;
  apply(clock);
  Frame ping{};
  ping.version = 1;
  ping.type = Ping;
  ping.session = 5;
  ping.attempt = 6;
  ping.utc = 1800000000;
  ping.target = 100;
  auto bad = incoming(ping, 1, 200);
  bad.hop_limit = 4;
  assert(intercept(bad));
  assert(!quiet.armed && !fakeRouter.sends);
  bad.hop_limit = 0;
  bad.want_ack = true;
  assert(intercept(bad));
  assert(!quiet.armed);
  bad.want_ack = false;
  bad.via_mqtt = true;
  assert(intercept(bad));
  assert(!quiet.armed);
  bad.via_mqtt = false;
  bad.survey_physical = 0;
  assert(intercept(bad));
  assert(!quiet.armed);
  bad.survey_physical = 1;
  bad.decoded.payload.size--;
  assert(intercept(bad));
  assert(!quiet.armed);
  bad.decoded.payload.size++;
  bad.decoded.payload.bytes[offsetof(Frame, group)] ^= 1;
  assert(intercept(bad));
  assert(!quiet.armed);
  meshtastic_MeshPacket queued{};
  queued.decoded.portnum = 67;
  classify(queued, true);
  assert(!suppress(queued));
  auto unrelated = queued;
  assert(!intercept(unrelated));
  if (!IS_CLIENT) {
    auto unknown = incoming(ping, 2, 900, false);
    assert(intercept(unknown));
    assert(quiet.remaining(ticks) == QUIET_MS);
    assert(!fakeRouter.sends);
    ticks += 3000;
    ping.utc += 3;
    ping.target = 400;
    auto nontarget = incoming(ping, 3, 200);
    assert(intercept(nontarget));
    assert(quiet.remaining(ticks) == QUIET_MS && !fakeRouter.sends);
    ping.target = 100;
    auto selected = incoming(ping, 4, 200);
    assert(intercept(selected));
    assert(fakeRouter.sends == 1);
    assert(!fakeRouter.packet.want_ack && !fakeRouter.packet.hop_limit &&
           !fakeRouter.packet.decoded.want_response);
    assert(intercept(selected));
    assert(fakeRouter.sends == 1);
    Frame pong{};
    memcpy(&pong, fakeRouter.packet.decoded.payload.bytes, sizeof pong);
    assert(pong.type == Pong && pong.rssi == -71 && pong.snrQuarter == 10 &&
           pong.attempt == 6);
    auto during = queued;
    classify(during, true);
    ticks += QUIET_MS;
    poll();
    assert(!quiet.armed && suppress(queued) && suppress(during));
    auto next = queued;
    classify(next, true);
    assert(!suppress(next));
    clockUtc = 0;
    loadFloor();
    assert(!acceptEpoch(ping.utc));
    ping.utc = restartFloor + 1;
    auto offline = incoming(ping, 10, 200);
    assert(intercept(offline));
    assert(fakeRouter.sends == 2);
  } else {
    auto target = make(Target, 3);
    target.count = 1;
    target.args[0] = 200;
    apply(target);
    auto beat = make(Heartbeat, 4);
    apply(beat);
    auto start = make(Start, 5);
    apply(start);
    assert(!running && previousError == 5);
    start.serial = 6;
    start.args[0] = 1;
    apply(start);
    assert(running && bench);
    apply(start);
    poll();
    auto id = pendingId;
    assert(id && fakeRouter.sends == 1);
    assert(!suppress(fakeRouter.packet));
    poll();
    assert(fakeRouter.sends == 1);
    txStart(fakeRouter.packet, 75, 600);
    ticks += 600;
    txDone(fakeRouter.packet);
    Frame pong{};
    pong.type = Pong;
    pong.version = 1;
    pong.session = session;
    pong.attempt = attempt;
    pong.target = 200;
    pong.utc = utc();
    auto wrong = pong;
    wrong.attempt++;
    auto badMatch = incoming(wrong, 11, 200);
    assert(intercept(badMatch));
    assert(pendingId == id);
    auto ok = incoming(pong, 12, 200);
    assert(intercept(ok));
    assert(successes == 1 && !pendingId);
    assert(intercept(ok));
    assert(successes == 1);
    ticks += 3000;
    poll();
    assert(pendingId);
    ticks += 7000;
    assert(suppress(fakeRouter.packet));
    poll();
    assert(paused && !running && !pendingId);
    auto stop = make(Stop, 7);
    apply(stop);
    apply(beat);
    assert(!running);
    meshtastic_Position loc{0, 0, 0, 1800000010, 1, 6};
    fix(loc, 100, 0, true, true);
    beat.serial = 8;
    apply(beat);
    start.serial = 9;
    start.args[0] = 0;
    apply(start);
    assert(running && !bench);
    poll();
    ticks += 2100;
    assert(suppress(fakeRouter.packet));
    poll();
    assert(paused && !running);
    fix(loc, 0, 0, true, true);
    assert(!fresh(
        gps, ticks)); // identical GNSS timestamp never refreshes the interlock
    for (unsigned i = 0; i < 30; ++i)
      snapshot();
    assert(dropped && count == 16);
  }
  puts(IS_CLIENT ? "survey client controller PASS"
                 : "survey responder controller PASS");
}
