#include "core/midi/SysExAssembler.h"

namespace korg {

namespace {

bool isRealtimeByte(quint8 byte)
{
    return byte >= 0xf8;
}

} // namespace

QByteArray takeSysExFrame(QByteArray *buffer)
{
    if (!buffer)
        return {};

    while (!buffer->isEmpty()) {
        const quint8 first = quint8((*buffer)[0]);
        if (isRealtimeByte(first) || (first >= 0x80 && first != 0xf0)) {
            buffer->remove(0, 1);
            continue;
        }
        if (first != 0xf0) {
            buffer->remove(0, 1);
            continue;
        }

        const int end = buffer->indexOf(char(0xf7));
        if (end < 0)
            return {};

        QByteArray frame = buffer->left(end + 1);
        buffer->remove(0, end + 1);

        QByteArray clean;
        clean.reserve(frame.size());
        for (char ch : frame) {
            if (!isRealtimeByte(quint8(ch)))
                clean.append(ch);
        }
        return clean;
    }
    return {};
}

} // namespace korg
