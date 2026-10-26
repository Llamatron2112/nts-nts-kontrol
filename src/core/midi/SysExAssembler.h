#pragma once

#include <QByteArray>

namespace korg {

// Extracts the next complete SysEx frame from buffer, ignoring realtime bytes
// (0xF8..0xFF) and non-SysEx status bytes outside frames. The returned frame is
// free of realtime bytes and includes the opening 0xF0 and closing 0xF7.
QByteArray takeSysExFrame(QByteArray *buffer);

} // namespace korg
