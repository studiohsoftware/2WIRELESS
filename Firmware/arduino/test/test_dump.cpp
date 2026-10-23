#include <cstdint>
#include <cstdio>
#include <cstring>

#include "minitest.h"
#include "wpm_dump.h"

// A started job (addr 0x28) that has already issued SEND_BACKUP at t0.
static void running(WpmDumpJob& j, uint32_t expected = 0, uint32_t first = 5000, uint32_t silence = 1000,
                    uint32_t t0 = 0) {
  CHECK(j.start(0x28, expected, first, silence, t0) == WPM_BUS_ERR_NONE);
  CHECK(j.step(t0) == WPM_DUMP_ACT_SEND_BACKUP);
}

int main() {
  {  // start_rejects_args
    WpmDumpJob j;
    CHECK(j.start(0x07, 0, 5000, 1000, 0) == WPM_BUS_ERR_ARGS);
    CHECK(j.start(0x78, 0, 5000, 1000, 0) == WPM_BUS_ERR_ARGS);
    CHECK(j.start(0x28, 0x2BD19, 5000, 1000, 0) == WPM_BUS_ERR_ARGS);
    CHECK(j.start(0x28, 0, 100, 1000, 0) == WPM_BUS_ERR_ARGS);
    CHECK(j.start(0x28, 0, 60001, 1000, 0) == WPM_BUS_ERR_ARGS);
    CHECK(j.start(0x28, 0, 5000, 30000, 0) == WPM_BUS_ERR_ARGS);
    CHECK(j.start(0x28, 0, 5000, 199, 0) == WPM_BUS_ERR_ARGS);
    CHECK(j.state() == WPM_DUMP_IDLE);
    CHECK(j.start(0x28, 0x2BD18, 200, 200, 0) == WPM_BUS_ERR_NONE);
  }
  {  // busy_while_running
    WpmDumpJob j;
    CHECK(j.start(0x28, 0, 5000, 1000, 0) == WPM_BUS_ERR_NONE);
    CHECK(j.start(0x28, 0, 5000, 1000, 0) == WPM_BUS_ERR_BUSY);
    CHECK(j.step(0) == WPM_DUMP_ACT_SEND_BACKUP);
    CHECK(j.start(0x28, 0, 5000, 1000, 1) == WPM_BUS_ERR_BUSY);
    CHECK(j.state() == WPM_DUMP_WAITING);
    j.on_data(0, 66);
    CHECK(j.step(10) == WPM_DUMP_ACT_NONE);
    CHECK(j.state() == WPM_DUMP_TRANSFERRING);
    CHECK(j.start(0x28, 0, 5000, 1000, 11) == WPM_BUS_ERR_BUSY);
  }
  {  // send_then_wait_then_fail_no_response
    WpmDumpJob j;
    CHECK(j.start(0x28, 0, 5000, 1000, 0) == WPM_BUS_ERR_NONE);
    CHECK(j.state() == WPM_DUMP_SENDING);
    CHECK(j.step(0) == WPM_DUMP_ACT_SEND_BACKUP);
    CHECK(j.state() == WPM_DUMP_WAITING);
    CHECK(j.step(1) == WPM_DUMP_ACT_NONE);
    CHECK(j.step(4999) == WPM_DUMP_ACT_NONE);
    CHECK(j.state() == WPM_DUMP_WAITING);
    CHECK(j.step(5000) == WPM_DUMP_ACT_NONE);
    CHECK(j.state() == WPM_DUMP_FAILED);
    CHECK(j.error() == WPM_BUS_ERR_NO_RESPONSE);
    CHECK(j.step(6000) == WPM_DUMP_ACT_NONE);
    CHECK(j.state() == WPM_DUMP_FAILED);
  }
  {  // send_failed_sets_timeout
    WpmDumpJob j;
    CHECK(j.start(0x28, 0, 5000, 1000, 0) == WPM_BUS_ERR_NONE);
    CHECK(j.step(0) == WPM_DUMP_ACT_SEND_BACKUP);
    j.send_failed(5);
    CHECK(j.state() == WPM_DUMP_FAILED);
    CHECK(j.error() == WPM_BUS_ERR_SEND_TIMEOUT);
  }
  {  // data_starts_transfer
    WpmDumpJob j;
    running(j);
    j.on_data(0, 66);
    CHECK(j.step(300) == WPM_DUMP_ACT_NONE);
    CHECK(j.state() == WPM_DUMP_TRANSFERRING);
    CHECK(j.received() == 66);
    CHECK(j.extent() == 66);
    CHECK(j.lines() == 1);
  }
  {  // completion_frame_ends_done
    WpmDumpJob j;
    running(j);
    j.on_data(0, 66);
    j.step(100);
    j.on_complete_frame(0x28);
    CHECK(j.step(150) == WPM_DUMP_ACT_NONE);
    CHECK(j.state() == WPM_DUMP_DONE);
    CHECK(j.complete_frame());
    CHECK(j.error() == WPM_BUS_ERR_NONE);
  }
  {  // completion_in_waiting: a completion frame before any data is an acknowledgement
    WpmDumpJob j;
    running(j);
    j.on_complete_frame(0x28);
    j.step(10);
    CHECK(j.state() == WPM_DUMP_WAITING);
    CHECK(j.early_done() == 1);
    CHECK(!j.complete_frame());
  }
  {  // early_completion_does_not_end_waiting
    WpmDumpJob j;
    running(j, 0, 5000, 1000, 0);
    j.on_complete_frame(0x28);
    j.step(26);
    CHECK(j.state() == WPM_DUMP_WAITING);
    CHECK(j.early_done() == 1);
    j.on_data(0, 66);
    j.step(3000);
    CHECK(j.state() == WPM_DUMP_TRANSFERRING);
    j.on_complete_frame(0x28);
    j.step(3010);
    CHECK(j.state() == WPM_DUMP_DONE);
    CHECK(j.complete_frame());
    CHECK(j.early_done() == 1);
    CHECK(j.error() == WPM_BUS_ERR_NONE);
  }
  {  // early_completion_then_first_wait_fails
    WpmDumpJob j;
    running(j, 0, 5000, 1000, 0);
    j.on_complete_frame(0x28);
    j.step(26);
    j.step(5000);
    CHECK(j.state() == WPM_DUMP_FAILED);
    CHECK(j.error() == WPM_BUS_ERR_NO_RESPONSE);
    CHECK(j.early_done() == 1);
    CHECK(!j.complete_frame());
  }
  {  // stray_completion_ignored
    WpmDumpJob j;
    running(j);
    j.on_data(0, 66);
    j.step(100);
    j.on_complete_frame(0x44);
    CHECK(j.stray_frames() == 1);
    CHECK(!j.complete_frame());
    j.step(200);
    CHECK(j.state() == WPM_DUMP_TRANSFERRING);
  }
  {  // expected_reached_ends_done
    WpmDumpJob j;
    running(j, 990);
    uint32_t t = 10;
    for (int i = 0; i < 15; i++) {
      j.on_data(static_cast<uint32_t>(i) * 66, 66);
      j.step(t);
      t += 10;
      if (i < 14) CHECK(j.state() == WPM_DUMP_TRANSFERRING);
    }
    j.step(t);
    CHECK(j.state() == WPM_DUMP_DONE);
    CHECK(j.received() == 990);
    CHECK(!j.complete_frame());
    CHECK(j.error() == WPM_BUS_ERR_NONE);
  }
  {  // silence_ends_done
    WpmDumpJob j;
    running(j, 0, 5000, 1000);
    j.on_data(0, 66);
    j.step(2000);
    CHECK(j.state() == WPM_DUMP_TRANSFERRING);
    CHECK(j.step(2999) == WPM_DUMP_ACT_NONE);
    CHECK(j.state() == WPM_DUMP_TRANSFERRING);
    j.step(3000);
    CHECK(j.state() == WPM_DUMP_DONE);
    CHECK(!j.complete_frame());
    CHECK(j.error() == WPM_BUS_ERR_NONE);
  }
  {  // activity_is_counter_change
    WpmDumpJob j;
    running(j, 0, 5000, 1000);
    j.on_data(0, 66);
    j.step(100);  // t_last = 100
    j.step(600);  // no change: must not refresh
    j.step(1099);
    CHECK(j.state() == WPM_DUMP_TRANSFERRING);
    j.step(1100);
    CHECK(j.state() == WPM_DUMP_DONE);
    // and a change does refresh
    WpmDumpJob k;
    running(k, 0, 5000, 1000);
    k.on_data(0, 66);
    k.step(100);
    k.on_data(66, 66);
    k.step(900);
    k.step(1899);
    CHECK(k.state() == WPM_DUMP_TRANSFERRING);
    k.step(1900);
    CHECK(k.state() == WPM_DUMP_DONE);
  }
  {  // first_wait_counts_only_data
    WpmDumpJob j;
    running(j, 0, 1000, 1000);
    j.on_complete_frame(0x44);  // stray
    j.on_dropped(10);           // dropped is not data written for this job
    j.step(500);
    CHECK(j.state() == WPM_DUMP_WAITING);
    j.step(1000);
    CHECK(j.state() == WPM_DUMP_FAILED);
    CHECK(j.error() == WPM_BUS_ERR_NO_RESPONSE);
  }
  {  // dropped_counts
    WpmDumpJob j;
    running(j);
    j.on_dropped(66);
    j.on_dropped(4);
    CHECK(j.dropped() == 70);
    CHECK(j.received() == 0);
  }
  {  // extent_vs_received
    WpmDumpJob j;
    running(j);
    j.on_data(0x100, 66);
    j.on_data(0x000, 66);
    j.step(10);
    CHECK(j.received() == 132);
    CHECK(j.extent() == 0x142);
    CHECK(j.lines() == 2);
  }
  {  // abort_returns_idle
    WpmDumpJob j;
    running(j);
    j.on_data(0, 66);
    j.step(10);
    j.abort(20);
    CHECK(j.state() == WPM_DUMP_IDLE);
    CHECK(j.received() == 66);
    CHECK(j.step(30) == WPM_DUMP_ACT_NONE);
    CHECK(j.state() == WPM_DUMP_IDLE);
    CHECK(j.start(0x28, 0, 5000, 1000, 40) == WPM_BUS_ERR_NONE);
  }
  {  // millis_wraparound
    WpmDumpJob j;
    uint32_t t0 = 0xFFFFFF00u;
    running(j, 0, 5000, 1000, t0);
    CHECK(j.elapsed_ms(t0 + 300) == 300);
    j.step(t0 + 4999);
    CHECK(j.state() == WPM_DUMP_WAITING);
    j.step(t0 + 5000);
    CHECK(j.state() == WPM_DUMP_FAILED);
    WpmDumpJob k;
    running(k, 0, 5000, 1000, t0);
    k.on_data(0, 66);
    k.step(t0 + 100);
    k.step(t0 + 1099);
    CHECK(k.state() == WPM_DUMP_TRANSFERRING);
    k.step(t0 + 1100);
    CHECK(k.state() == WPM_DUMP_DONE);
  }
  {  // restart_clears_counters
    WpmDumpJob j;
    running(j);
    j.on_data(0, 66);
    j.on_dropped(3);
    j.on_complete_frame(0x44);
    j.on_complete_frame(0x28);
    j.step(10);
    CHECK(j.state() == WPM_DUMP_DONE);
    CHECK(j.start(0x44, 100, 5000, 1000, 50) == WPM_BUS_ERR_NONE);
    CHECK(j.addr() == 0x44);
    CHECK(j.expected() == 100);
    CHECK(j.received() == 0);
    CHECK(j.extent() == 0);
    CHECK(j.lines() == 0);
    CHECK(j.dropped() == 0);
    CHECK(j.stray_frames() == 0);
    CHECK(!j.complete_frame());
    CHECK(j.error() == WPM_BUS_ERR_NONE);
    CHECK(j.state() == WPM_DUMP_SENDING);
  }
  {  // status_json_exact
    char buf[256];
    WpmDumpJob idle;
    size_t n = wpm_dump_status_json(idle, 12345, buf, sizeof buf);
    const char* want_idle =
        "{\"state\":\"idle\",\"addr\":\"00\",\"expected\":0,\"received\":0,\"extent\":0,\"lines\":0,"
        "\"dropped\":0,\"complete_frame\":false,\"early_done\":0,\"error\":0,\"elapsed_ms\":0}";
    CHECK(n == std::strlen(want_idle));
    CHECK(std::strcmp(buf, want_idle) == 0);

    WpmDumpJob j;
    running(j, 990, 5000, 1000, 0);
    for (int i = 0; i < 8; i++) j.on_data(static_cast<uint32_t>(i) * 66, 66);
    j.step(1000);
    n = wpm_dump_status_json(j, 1234, buf, sizeof buf);
    const char* want =
        "{\"state\":\"transferring\",\"addr\":\"28\",\"expected\":990,\"received\":528,\"extent\":528,"
        "\"lines\":8,\"dropped\":0,\"complete_frame\":false,\"early_done\":0,\"error\":0,\"elapsed_ms\":1234}";
    CHECK(n == std::strlen(want));
    CHECK(std::strcmp(buf, want) == 0);
    CHECK(wpm_dump_status_json(j, 1234, buf, 20) == 0);
  }
  {  // elapsed_frozen_after_done_and_abort
    WpmDumpJob j;
    running(j, 0, 5000, 1000, 0);
    j.on_data(0, 10);
    j.step(100);
    CHECK(j.elapsed_ms(500) == 500);
    j.step(1100);  // silence -> done at 1100
    CHECK(j.state() == WPM_DUMP_DONE);
    CHECK(j.elapsed_ms(9999) == 1100);
    j.abort(5000);  // not running: end time unchanged
    CHECK(j.elapsed_ms(9999) == 1100);
    WpmDumpJob k;
    running(k, 0, 5000, 1000, 0);
    k.abort(700);
    CHECK(k.state() == WPM_DUMP_IDLE);
    CHECK(k.elapsed_ms(5000) == 700);
  }
  {  // send_failed_noop_outside_sending_waiting
    WpmDumpJob j;
    j.send_failed(1);  // idle
    CHECK(j.state() == WPM_DUMP_IDLE);
    CHECK(j.error() == WPM_BUS_ERR_NONE);
    running(j, 0, 5000, 1000, 0);
    j.on_data(0, 10);
    j.step(10);
    CHECK(j.state() == WPM_DUMP_TRANSFERRING);
    j.send_failed(20);
    CHECK(j.state() == WPM_DUMP_TRANSFERRING);
    CHECK(j.error() == WPM_BUS_ERR_NONE);
    j.on_complete_frame(0x28);
    j.step(30);
    CHECK(j.state() == WPM_DUMP_DONE);
    j.send_failed(40);
    CHECK(j.state() == WPM_DUMP_DONE);
    CHECK(j.error() == WPM_BUS_ERR_NONE);
  }
  {  // start_refusal_keeps_counters
    WpmDumpJob j;
    running(j, 0, 5000, 1000, 0);
    j.on_data(0x100, 66);
    j.on_dropped(3);
    j.step(10);
    CHECK(j.start(0x28, 0, 5000, 1000, 11) == WPM_BUS_ERR_BUSY);  // busy
    CHECK(j.received() == 66 && j.extent() == 0x142 && j.lines() == 1 && j.dropped() == 3);
    j.abort(20);
    CHECK(j.start(0x07, 0, 5000, 1000, 21) == WPM_BUS_ERR_ARGS);  // args
    CHECK(j.state() == WPM_DUMP_IDLE);
    CHECK(j.received() == 66 && j.extent() == 0x142 && j.lines() == 1 && j.dropped() == 3);
    CHECK(j.elapsed_ms(99) == 20);
  }
  {  // short_frame_ignored / classify
    CHECK(wpm_dump_classify(0x50, 2, 0, 0, true) == WPM_DUMP_FRAME_IGNORE);
    CHECK(wpm_dump_classify(0x50, 3, 0, 0, true) == WPM_DUMP_FRAME_DATA);
    CHECK(wpm_dump_classify(0x50, 5, 0x04, 0x0A, true) == WPM_DUMP_FRAME_DATA);  // a card line, even if it looks like a completion
    CHECK(wpm_dump_classify(0x00, 5, 0x04, 0x0A, true) == WPM_DUMP_FRAME_COMPLETE);
    CHECK(wpm_dump_classify(0x00, 5, 0x08, 0x0F, true) == WPM_DUMP_FRAME_OTHER);  // MIDI clock
    CHECK(wpm_dump_classify(0x00, 11, 0x04, 0x13, true) == WPM_DUMP_FRAME_OTHER);
    CHECK(wpm_dump_classify(0xFF, 5, 0x04, 0x0A, true) == WPM_DUMP_FRAME_OTHER);
    CHECK(wpm_dump_classify(0xFF, 5, 0x04, 0x0A, false) == WPM_DUMP_FRAME_COMPLETE);
    CHECK(wpm_dump_classify(0xFF, 5, 0x08, 0x0F, false) == WPM_DUMP_FRAME_DATA);
    CHECK(wpm_dump_classify(0xFF, 2, 0, 0, false) == WPM_DUMP_FRAME_IGNORE);
  }
  {  // offset_out_of_range_dropped / plan_line
    WpmDumpLinePlan p = wpm_dump_plan_line(0x01, 0x00, 68, 0, false);
    CHECK(p.offset == 0x100 && p.write_n == 66 && p.dropped == 0);
    p = wpm_dump_plan_line(0x01, 0x00, 68, 0, false, 0x120);  // 32 fit
    CHECK(p.write_n == 32 && p.dropped == 34);
    p = wpm_dump_plan_line(0x01, 0x40, 8, 0, false, 0x120);  // starts beyond the end
    CHECK(p.write_n == 0 && p.dropped == 6);
    p = wpm_dump_plan_line(0x03, 0xDE, 68, 990, true);  // offset 990 == expected: dropped, not written
    CHECK(p.write_n == 0 && p.dropped == 66);
    p = wpm_dump_plan_line(0x03, 0xDD, 4, 990, true);  // offset 989 < expected
    CHECK(p.write_n == 2 && p.dropped == 0);
    p = wpm_dump_plan_line(0x03, 0xDE, 4, 0, true);  // expected unknown: never dropped
    CHECK(p.write_n == 2);
  }
  {  // parse_args
    uint8_t a = 0;
    uint32_t len, fw, sil;
    CHECK(wpm_dump_parse_args("addr=28", &a, &len, &fw, &sil) && a == 0x28 && len == 0 && fw == 5000 && sil == 1000);
    CHECK(wpm_dump_parse_args("addr=2A&len=990&first_wait_ms=100&silence_ms=99999", &a, &len, &fw, &sil));
    CHECK(a == 0x2A && len == 990 && fw == WPM_DUMP_FIRST_WAIT_MIN_MS && sil == WPM_DUMP_SILENCE_MAX_MS);
    CHECK(wpm_dump_parse_args("silence_ms=500&addr=5e", &a, &len, &fw, &sil) && a == 0x5E && sil == 500);
    CHECK(!wpm_dump_parse_args("", &a, &len, &fw, &sil));                    // addr required
    CHECK(!wpm_dump_parse_args("len=5", &a, &len, &fw, &sil));
    CHECK(!wpm_dump_parse_args("addr=28&bogus=1", &a, &len, &fw, &sil));     // unknown key
    CHECK(!wpm_dump_parse_args("addr=28&len=", &a, &len, &fw, &sil));        // empty value
    CHECK(!wpm_dump_parse_args("addr=28&len", &a, &len, &fw, &sil));         // no '='
    CHECK(!wpm_dump_parse_args("addr=28&len=1234567890", &a, &len, &fw, &sil));  // 10 digits
    CHECK(wpm_dump_parse_args("addr=28&len=123456789", &a, &len, &fw, &sil) && len == 123456789u);
    CHECK(!wpm_dump_parse_args("addr=2&len=1", &a, &len, &fw, &sil));
    CHECK(!wpm_dump_parse_args("addr=2G", &a, &len, &fw, &sil));
    CHECK(!wpm_dump_parse_args("addr=028", &a, &len, &fw, &sil));
    CHECK(!wpm_dump_parse_args("addr=28&len=12x", &a, &len, &fw, &sil));
  }
  {  // state strings
    CHECK(std::strcmp(wpm_dump_state_str(WPM_DUMP_IDLE), "idle") == 0);
    CHECK(std::strcmp(wpm_dump_state_str(WPM_DUMP_SENDING), "sending") == 0);
    CHECK(std::strcmp(wpm_dump_state_str(WPM_DUMP_WAITING), "waiting") == 0);
    CHECK(std::strcmp(wpm_dump_state_str(WPM_DUMP_TRANSFERRING), "transferring") == 0);
    CHECK(std::strcmp(wpm_dump_state_str(WPM_DUMP_DONE), "done") == 0);
    CHECK(std::strcmp(wpm_dump_state_str(WPM_DUMP_FAILED), "failed") == 0);
  }
  std::printf("test_dump: %d failures\n", minitest_failures);
  return minitest_failures;
}
