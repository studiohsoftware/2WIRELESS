#include <cstdint>
#include <cstdio>
#include <cstring>

#include "minitest.h"
#include "wpm_bus_scan.h"

static void reply(WpmBusScan& s, uint8_t a, const char* v, size_t n = 7) {
  s.on_version_frame(a, reinterpret_cast<const uint8_t*>(v), n);
}

static void ack(WpmBusScan& s, uint8_t a) { s.on_query_reply(a); }

int main() {
  {  // start_rejects_bad_args
    WpmBusScan s;
    CHECK(s.start(0x07, 0x28, 0) == WPM_BUS_ERR_ARGS);
    CHECK(s.start(0x28, 0x78, 0) == WPM_BUS_ERR_ARGS);
    CHECK(s.state() == WPM_SCAN_IDLE);
    CHECK(s.last_error() == WPM_BUS_ERR_ARGS);
  }
  {  // reversed_range_is_empty_done
    WpmBusScan s;
    CHECK(s.start(0x30, 0x28, 0) == WPM_BUS_ERR_NONE);
    CHECK(s.step(0) == WPM_SCAN_ACT_NONE);
    CHECK(s.state() == WPM_SCAN_DONE);
    CHECK(s.count() == 0);
    CHECK(s.idx() == 0);
  }
  {  // busy_while_scanning
    WpmBusScan s;
    CHECK(s.start(0x28, 0x29, 0) == WPM_BUS_ERR_NONE);
    CHECK(s.start(0x28, 0x29, 0) == WPM_BUS_ERR_BUSY);
    CHECK(s.last_error() == WPM_BUS_ERR_BUSY);
  }
  {  // single_address_answers (identity ACK)
    WpmBusScan s;
    s.start(0x28, 0x28, 1000);
    CHECK(s.step(1000) == WPM_SCAN_ACT_QUERY);
    CHECK(s.current_addr() == 0x28);
    CHECK(s.step(1100) == WPM_SCAN_ACT_NONE);
    ack(s, 0x28);
    CHECK(s.step(1150) == WPM_SCAN_ACT_NONE);  // ACK alone waits out the window
    CHECK(s.count() == 0);
    CHECK(s.step(1300) == WPM_SCAN_ACT_NONE);
    CHECK(s.count() == 1);
    CHECK(s.state() == WPM_SCAN_SCANNING);
    CHECK(s.step(1300 + 150) == WPM_SCAN_ACT_NONE);
    CHECK(s.state() == WPM_SCAN_DONE);
    CHECK(s.found()[0].addr == 0x28);
    CHECK(!s.found()[0].ver_valid);
    CHECK(s.idx() == 1);
    CHECK(s.reply_ms() == 300);
  }
  {  // custom_window
    WpmBusScan s;
    s.start(0x10, 0x10, 0, 1000);
    CHECK(s.reply_ms() == 1000);
    CHECK(s.step(0) == WPM_SCAN_ACT_QUERY);
    CHECK(s.step(999) == WPM_SCAN_ACT_NONE);
    CHECK(s.state() == WPM_SCAN_SCANNING);
    s.step(1000);
    CHECK(s.state() == WPM_SCAN_SCANNING);
    s.step(1150);
    CHECK(s.state() == WPM_SCAN_DONE);
    CHECK(s.count() == 0);
  }
  {  // no_answer_times_out
    WpmBusScan s;
    s.start(0x10, 0x10, 0);
    CHECK(s.step(0) == WPM_SCAN_ACT_QUERY);
    CHECK(s.step(299) == WPM_SCAN_ACT_NONE);
    CHECK(s.state() == WPM_SCAN_SCANNING);
    s.step(300);
    s.step(449);
    CHECK(s.state() == WPM_SCAN_SCANNING);
    s.step(450);
    CHECK(s.state() == WPM_SCAN_DONE);
    CHECK(s.count() == 0);
    CHECK(s.last_error() == WPM_BUS_ERR_NONE);
  }
  {  // two_addresses_second_answers
    WpmBusScan s;
    s.start(0x10, 0x11, 0);
    CHECK(s.step(0) == WPM_SCAN_ACT_QUERY);
    CHECK(s.current_addr() == 0x10);
    s.step(300);
    CHECK(s.step(450) == WPM_SCAN_ACT_QUERY);
    CHECK(s.current_addr() == 0x11);
    ack(s, 0x11);
    s.step(750);
    s.step(900);
    CHECK(s.state() == WPM_SCAN_DONE);
    CHECK(s.count() == 1);
    CHECK(s.found()[0].addr == 0x11);
    CHECK(s.idx() == 2);
  }
  {  // stray_reply_ignored
    WpmBusScan s;
    s.start(0x28, 0x28, 0);
    s.step(0);
    ack(s, 0x44);
    s.step(10);
    CHECK(s.count() == 0);
    CHECK(s.stray_replies() == 1);
    ack(s, 0x28);
    s.step(300);
    CHECK(s.count() == 1);
    CHECK(s.found()[0].addr == 0x28);
  }
  {  // late_reply_ignored
    WpmBusScan s;
    s.start(0x28, 0x28, 0);
    s.step(0);
    s.step(300);
    ack(s, 0x28);
    reply(s, 0x28, "31.0   ");
    s.step(310);
    CHECK(s.count() == 0);
    CHECK(s.stray_replies() == 2);
  }
  {  // ack_only_entry_has_no_version
    WpmBusScan s;
    s.start(0x28, 0x28, 0);
    s.step(0);
    ack(s, 0x28);
    s.step(300);
    CHECK(s.count() == 1);
    CHECK(!s.found()[0].ver_valid);
    CHECK(std::memcmp(s.found()[0].ver, "       ", 7) == 0);
  }
  {  // ack_then_version_in_window
    WpmBusScan s;
    s.start(0x28, 0x28, 0);
    s.step(0);
    ack(s, 0x28);
    s.step(50);  // ACK alone: still waiting
    CHECK(s.count() == 0);
    reply(s, 0x28, "31.0   ");
    s.step(60);
    CHECK(s.count() == 1);
    CHECK(s.found()[0].ver_valid);
    CHECK(std::memcmp(s.found()[0].ver, "31.0   ", 7) == 0);
    CHECK(s.stray_replies() == 0);
  }
  {  // version_then_ack_in_window
    WpmBusScan s;
    s.start(0x28, 0x28, 0);
    s.step(0);
    reply(s, 0x28, "31.0   ");
    ack(s, 0x28);
    s.step(60);
    CHECK(s.count() == 1);
    CHECK(s.found()[0].ver_valid);
    CHECK(std::memcmp(s.found()[0].ver, "31.0   ", 7) == 0);
  }
  {  // version_frame_alone_counts_as_answer
    WpmBusScan s;
    s.start(0x28, 0x28, 0);
    s.step(0);
    reply(s, 0x28, "31.0   ");
    s.step(5);
    CHECK(s.count() == 1);
    CHECK(s.found()[0].ver_valid);
  }
  {  // stray_ack_ignored
    WpmBusScan s;
    s.start(0x28, 0x28, 0);
    s.step(0);
    ack(s, 0x29);
    s.step(300);
    CHECK(s.count() == 0);
    CHECK(s.stray_replies() == 1);
  }
  {  // short_version_marks_invalid + entry json
    WpmBusScan s;
    s.start(0x5c, 0x5c, 0);
    s.step(0);
    reply(s, 0x5c, "251e", 4);
    s.step(10);
    CHECK(s.count() == 1);
    CHECK(!s.found()[0].ver_valid);
    CHECK(std::memcmp(s.found()[0].ver, "251e   ", 7) == 0);
    char buf[96];
    size_t n = wpm_scan_entry_json(s.found()[0], buf, sizeof buf);
    CHECK(std::strcmp(buf, "{\"addr\":\"5c\",\"ver_valid\":false,\"version\":\"251e   \",\"ver_src\":\"none\"}") == 0);
    CHECK(n == std::strlen(buf));
  }
  {  // send_failure_continues_sweep
    WpmBusScan s;
    s.start(0x10, 0x11, 0);
    CHECK(s.step(0) == WPM_SCAN_ACT_QUERY);
    s.query_failed(0);
    CHECK(s.last_error() == WPM_BUS_ERR_SEND_TIMEOUT);
    CHECK(s.step(149) == WPM_SCAN_ACT_NONE);
    CHECK(s.step(150) == WPM_SCAN_ACT_QUERY);
    CHECK(s.current_addr() == 0x11);
    s.step(450);
    s.step(600);
    CHECK(s.state() == WPM_SCAN_DONE);
  }
  {  // restart_after_done_clears
    WpmBusScan s;
    s.start(0x28, 0x28, 0);
    s.step(0);
    reply(s, 0x44, "259e   ");
    reply(s, 0x28, "31.0   ");
    s.step(10);
    s.step(200);
    CHECK(s.state() == WPM_SCAN_DONE);
    CHECK(s.count() == 1);
    CHECK(s.stray_replies() == 1);
    CHECK(s.start(0x28, 0x28, 1000) == WPM_BUS_ERR_NONE);
    CHECK(s.count() == 0);
    CHECK(s.idx() == 0);
    CHECK(s.stray_replies() == 0);
    CHECK(s.last_error() == WPM_BUS_ERR_NONE);
  }
  {  // entry_json_exact / escapes
    WpmScanFound f = {0x28, true, {'3', '1', '.', '0', ' ', ' ', ' '}, WPM_VER_SRC_FRAME};
    char buf[96];
    wpm_scan_entry_json(f, buf, sizeof buf);
    CHECK(std::strcmp(buf, "{\"addr\":\"28\",\"ver_valid\":true,\"version\":\"31.0   \",\"ver_src\":\"frame\"}") == 0);
    WpmScanFound g = {0x0a, true, {'a', '"', 'b', '\\', 'c', 0x01, static_cast<char>(0x80)}, WPM_VER_SRC_NONE};
    wpm_scan_entry_json(g, buf, sizeof buf);
    CHECK(std::strcmp(buf, "{\"addr\":\"0a\",\"ver_valid\":true,\"version\":\"a\\\"b\\\\c..\",\"ver_src\":\"none\"}") == 0);
  }
  {  // entry_json_ver_src_values
    WpmScanFound f = {0x28, true, {'3', '1', '.', '0', ' ', ' ', ' '}, WPM_VER_SRC_CACHE};
    char buf[96];
    wpm_scan_entry_json(f, buf, sizeof buf);
    CHECK(std::strcmp(buf, "{\"addr\":\"28\",\"ver_valid\":true,\"version\":\"31.0   \",\"ver_src\":\"cache\"}") == 0);
    CHECK(std::strcmp(wpm_scan_ver_src_str(WPM_VER_SRC_NONE), "none") == 0);
    CHECK(std::strcmp(wpm_scan_ver_src_str(WPM_VER_SRC_FRAME), "frame") == 0);
    CHECK(std::strcmp(wpm_scan_ver_src_str(9), "none") == 0);
  }
  {  // status_head_exact
    char buf[96];
    wpm_scan_status_head_json(WPM_SCAN_SCANNING, 3, 1, buf, sizeof buf);
    CHECK(std::strcmp(buf, "{\"state\":\"scanning\",\"idx\":3,\"count\":1,\"found\":[") == 0);
    wpm_scan_status_head_json(WPM_SCAN_IDLE, 0, 0, buf, sizeof buf);
    CHECK(std::strcmp(buf, "{\"state\":\"idle\",\"idx\":0,\"count\":0,\"found\":[") == 0);
  }
  {  // route_match_order
    const char* c[] = {"/bus/scan/status", "/bus/scan", "/bus/status", "/bus/polldone"};
    CHECK(wpm_bus_route_match("GET /bus/scan/status HTTP/1.1", c, 4) == 0);
    CHECK(wpm_bus_route_match("GET /bus/scan?first=08&last=77 HTTP/1.1", c, 4) == 1);
    CHECK(wpm_bus_route_match("GET /bus/status HTTP/1.1", c, 4) == 2);
    CHECK(wpm_bus_route_match("GET /bus/polldone HTTP/1.1", c, 4) == 3);
    CHECK(wpm_bus_route_match("GET /currentpreset HTTP/1.1", c, 4) == -1);
  }
  {  // millis_wraparound
    WpmBusScan s;
    uint32_t t = 0xFFFFFF00u;
    s.start(0x10, 0x10, t);
    CHECK(s.step(t) == WPM_SCAN_ACT_QUERY);
    CHECK(s.step(t + 299) == WPM_SCAN_ACT_NONE);  // wraps past 0 here
    CHECK(s.state() == WPM_SCAN_SCANNING);
    s.step(t + 300);  // window elapsed, gap starts
    s.step(t + 449);
    CHECK(s.state() == WPM_SCAN_SCANNING);
    s.step(t + 450);
    CHECK(s.state() == WPM_SCAN_DONE);
    CHECK(s.count() == 0);
  }
  {  // second_frame_is_stray
    WpmBusScan s;
    s.start(0x28, 0x28, 0);
    s.step(0);
    reply(s, 0x28, "31.0   ");
    reply(s, 0x28, "99.9   ");
    CHECK(s.stray_replies() == 1);
    s.step(10);
    CHECK(s.count() == 1);
    CHECK(std::memcmp(s.found()[0].ver, "31.0   ", 7) == 0);
  }
  {  // query_failed_guarded
    WpmBusScan s;
    s.query_failed(0);  // idle: no-op
    CHECK(s.last_error() == WPM_BUS_ERR_NONE);
    s.start(0x10, 0x10, 0);
    s.query_failed(0);  // SEND phase, no QUERY yet: no-op
    CHECK(s.last_error() == WPM_BUS_ERR_NONE);
  }
  {  // parse_scan_args
    uint8_t f, l;
    uint32_t w;
    CHECK(wpm_bus_parse_scan_args("", &f, &l, &w) && f == 0x08 && l == 0x77 && w == 300);
    CHECK(wpm_bus_parse_scan_args("first=5C&last=5e", &f, &l, &w) && f == 0x5c && l == 0x5e && w == 300);
    CHECK(!wpm_bus_parse_scan_args("first=8", &f, &l, &w));
    CHECK(!wpm_bus_parse_scan_args("first=0g", &f, &l, &w));
    CHECK(!wpm_bus_parse_scan_args("window=", &f, &l, &w));
    CHECK(wpm_bus_parse_scan_args("window=99", &f, &l, &w) && w == 100);
    CHECK(wpm_bus_parse_scan_args("window=5000", &f, &l, &w) && w == 2000);
    CHECK(wpm_bus_parse_scan_args("window=450", &f, &l, &w) && w == 450);
    CHECK(!wpm_bus_parse_scan_args("window=1234567", &f, &l, &w));
    CHECK(!wpm_bus_parse_scan_args("window=12a", &f, &l, &w));
    CHECK(wpm_bus_parse_scan_args("foo=1&last=10&bar", &f, &l, &w) && f == 0x08 && l == 0x10);
  }
  {  // found_cap
    WpmBusScan s;
    CHECK(s.start(WPM_SCAN_ADDR_MIN, WPM_SCAN_ADDR_MAX, 0) == WPM_BUS_ERR_NONE);
    uint32_t now = 0;
    int guard = 0;
    while (s.state() == WPM_SCAN_SCANNING && guard++ < 100000) {
      if (s.step(now) == WPM_SCAN_ACT_QUERY) reply(s, s.current_addr(), "31.0   ");
      now += 10;
    }
    CHECK(s.state() == WPM_SCAN_DONE);
    CHECK(s.count() == WPM_SCAN_MAX_FOUND);
    CHECK(s.idx() == WPM_SCAN_MAX_FOUND);
  }
  std::printf("test_bus_scan: %d failures\n", minitest_failures);
  return minitest_failures;
}
