#include "zb_mirror.h"
#include "zb_report.h"

#include <stdarg.h>

// Room for a whole console line. The mirror keeps MIRROR_TEXT_LEN of it, but the
// console gets everything, and the longest line that goes through logEvent() today
// is around sixty characters - so this is the limit of what can be printed, not the
// limit of what is printed.
//
// Not LINE_MAX: that is a POSIX limit macro (2048), which Arduino.h drags in
// through sys/param.h, so the name expands to a number before the compiler ever
// sees the declaration.
static constexpr size_t CONSOLE_LINE_LEN = 160;

// The stack refuses a report whose string is longer than 50 characters - see
// MIRROR_TEXT_LEN in config.h - and the line is padded to MIRROR_TEXT_LEN, so above
// that every single line would be refused. Caught here rather than in the field,
// which is where 64 and 56 were found out.
static_assert(MIRROR_TEXT_LEN <= 50, "MIRROR_TEXT_LEN above 50: the stack refuses every mirror report as too big");

void logEvent(const char *fmt, ...) {
  char line[CONSOLE_LINE_LEN];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  Serial.printf("%s" CONSOLE_EOL, line);  // one write, ending like every other line - see CONSOLE_EOL
  zbMirror.mirror(line);
}

void ZbMirror::mirror(const char *line) {
  if (!ZB_MIRROR_ENDPOINT) {
    return;  // no endpoint in this build, so the line stays on the console
  }

  // The console indents a line under the header it belongs to. The mirror carries
  // one line on its own, where an indent means nothing.
  while (*line == ' ') {
    line++;
  }
  // A line longer than the mirror carries is cut, and the coordinator gets its
  // start with nothing to say the rest is missing. The console has the whole line -
  // logEvent() printed it just before this - so a note right under it is enough to
  // find the culprit. Most lines are fixed wording, and test/logevent_len catches
  // those before they are ever flashed; this is for the ones put together at run
  // time, a setting's value or a sensor name, which only show at run time.
  // Serial.printf(), never logEvent(): this line must not be mirrored itself.
  size_t len = strlen(line);
  if (len > MIRROR_TEXT_LEN) {
    Serial.printf("Console mirror (EP %u): line above cut to %u of %u characters" CONSOLE_EOL, _endpoint,
                  (unsigned)MIRROR_TEXT_LEN, (unsigned)len);
  }
  strncpy(_text, line, MIRROR_TEXT_LEN);
  _text[MIRROR_TEXT_LEN] = '\0';

  // A line the coordinator already has changes nothing on the air. This is the
  // same deadband the temperatures and the link values use: a read that fails
  // every interval, or a slot that stays missing, then costs one report instead of
  // one per interval. The heartbeat still repeats it.
  if (strcmp(_text, _published) == 0) {
    return;
  }

  // Counts every line the mirror was given, whether or not the radio could deliver
  // it. So a gap in the sequence is honest: it says lines were printed while the
  // device was off the air.
  _sequence++;
  publish();
}

void ZbMirror::forgetPublished() {
  _published[0] = '\0';
}

void ZbMirror::handleReports() {
  if (!ZB_MIRROR_ENDPOINT || MIRROR_REPORT_HEARTBEAT_S == 0) {
    return;
  }
  if (!Zigbee.connected() || _text[0] == '\0') {
    return;  // nobody to repeat it to, or nothing printed yet
  }
  if ((millis() - _lastReportMs) < (uint32_t)MIRROR_REPORT_HEARTBEAT_S * 1000UL) {
    return;
  }
  publish();
}

void ZbMirror::publish() {
  if (!Zigbee.connected()) {
    // Off the air there is nobody to report to, and _published is left alone on
    // purpose: the line is still pending, and the next join sends it.
    return;
  }

  // The text goes first and the sequence number second, so that a coordinator
  // acting on a new number already has the line that number belongs to.
  // The first step that failed, so the console can say which one it was: setting an
  // attribute is local and fails for a different reason than a report does.
  const char *failed = nullptr;
  if (_hasText) {
    if (!setText()) {
      failed = "setting the text failed";
    } else if (!reportText()) {  // a write that failed is not worth reporting
      failed = "reporting the text failed";
    }
  }
  if (!setAnalogInput(_sequence)) {
    failed = failed ? failed : "setting the counter failed";
  } else if (!reportAnalogInput()) {
    failed = failed ? failed : "reporting the counter failed";
  }
  bool ok = failed == nullptr;

  // Whether the line made it onto the air is otherwise invisible. Every failure
  // below this function says so through log_e() or log_w(), and Core Debug Level is
  // "None" in the board settings this is built with, which compiles those away: a
  // mirror that had stopped working looked exactly like a coordinator that ignores
  // it, and the console - where the line does appear, because logEvent() prints
  // before it mirrors - gave no hint which of the two it was.
  //
  // Serial.printf(), never logEvent(): logEvent() hands its line to mirror(), which
  // comes straight back here, fails again, and says so again.
  //
  // Said once per spell of failing, and again once it recovers, because whatever
  // stops the mirror stops it for every line: a bus that has gone quiet would
  // otherwise turn one failure into a failure message per reading.
  if (ok != _publishWorked) {
    if (ok) {
      Serial.printf("Console mirror (EP %u): sending again" CONSOLE_EOL, _endpoint);
    } else {
      Serial.printf("Console mirror (EP %u): cannot be sent, %s - the console is all there is for now" CONSOLE_EOL,
                    _endpoint, failed);
    }
    _publishWorked = ok;
  }

  strcpy(_published, _text);
  _lastReportMs = millis();
}

// A ZCL character string is stored with a leading length byte, and the stack sizes
// the attribute from the value it is created with - the same thing
// TempEndpoint::setSensorId() works around. So addText() creates it at its full
// length, all spaces, and every line written afterwards keeps that length, so it
// always fits the room that reserved. The placeholder itself is never seen: the
// first line published is the join, and before the join there is no coordinator to
// read anything.

bool ZbMirror::setText() {
  if (!_hasText) {
    return false;
  }

  // Space padded to the full length on every write, not only on the first, exactly
  // as TempEndpoint::setSensorId() pads the sensor id: the stack copies a string
  // by the length byte it is handed, and how much it copies when a shorter value
  // lands on a longer one is its business, not ours. So the line goes out with
  // trailing spaces, which is a receiver's trim() away - see README.md.
  size_t len = strlen(_text);  // mirror() has already cut this to MIRROR_TEXT_LEN
  char zcl[MIRROR_TEXT_LEN + 2];
  zcl[0] = (char)MIRROR_TEXT_LEN;
  memset(zcl + 1, ' ', MIRROR_TEXT_LEN);
  memcpy(zcl + 1, _text, len);
  zcl[MIRROR_TEXT_LEN + 1] = '\0';

  esp_zb_zcl_status_t status = setClusterAttribute(ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
                                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, MIRROR_TEXT_ATTR_ID, (void *)zcl);
  if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
    log_e("Failed to update the mirrored line: %s", esp_zb_zcl_status_to_name(status));
    return false;
  }
  return true;
}

bool ZbMirror::reportText() {
  // The library reports the attributes it knows about, not this one - see zb_report.h
  // for how every report is built and where it goes.
  return zbReport(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, MIRROR_TEXT_ATTR_ID);
}

bool ZbMirror::reportAnalogInput() {
  // Hides the core's version, which reports through the binding table - see zb_report.h.
  return zbReport(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
}

void ZbMirror::setTextReporting(uint16_t minInterval, uint16_t maxInterval) {
  if (!_hasText) {
    return;
  }
  // Nothing else creates a reporting entry for this attribute - see zbSetReporting().
  zbSetReporting(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, MIRROR_TEXT_ATTR_ID, minInterval, maxInterval);
}

bool ZbMirror::addText() {
  esp_zb_attribute_list_t *cluster = esp_zb_cluster_list_get_cluster(
    _cluster_list, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  if (cluster == nullptr) {
    log_e("Failed to get analog input cluster for the mirrored line");
    return false;
  }

  char placeholder[MIRROR_TEXT_LEN + 2];
  placeholder[0] = (char)MIRROR_TEXT_LEN;
  memset(placeholder + 1, ' ', MIRROR_TEXT_LEN);
  placeholder[MIRROR_TEXT_LEN + 1] = '\0';

  // There is no cluster specific add_attr for an attribute the standard does not
  // define, so the generic one is used, and it is the call that says what the
  // attribute is: a character string that can be read and reported, never written.
  esp_err_t ret = esp_zb_cluster_add_attr(cluster, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, MIRROR_TEXT_ATTR_ID,
                                          ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                                          (void *)placeholder);
  if (ret != ESP_OK) {
    log_w("Failed to add the mirrored line attribute: 0x%x: %s", ret, esp_err_to_name(ret));
    return false;
  }
  _hasText = true;
  return true;
}
