#include "mesh/SnorrSniffer.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    const uint8_t golden[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    assert(snorr::snifferCrc(golden, sizeof(golden)) == 0xcbf43926U);
    uint8_t raw[255];
    for (unsigned i = 0; i < sizeof(raw); ++i)
        raw[i] = i;
    for (unsigned length : {0U, 1U, 16U, 40U, 41U, 255U}) {
        std::vector<std::string> lines;
        snorr::emitSnifferReception([&](const char *line) { lines.emplace_back(line); }, UINT32_MAX, UINT32_MAX, UINT32_MAX, raw,
                                    length, -32768, -128, -400.0f, UINT32_MAX);
        assert(lines.size() == 2 + (length + 39) / 40);
        assert(lines.front().find("seq=4294967295") != std::string::npos);
        unsigned offset = 0;
        for (const auto &line : lines)
            assert(line.size() < 151);
        for (unsigned index = 1; index + 1 < lines.size(); ++index) {
            unsigned seq, foundOffset;
            char hex[81];
            assert(sscanf(lines[index].c_str(), "SNORR_DATA seq=%u off=%u hex=%80s", &seq, &foundOffset, hex) == 3);
            assert(seq == UINT32_MAX && foundOffset == offset);
            for (size_t pos = 0; pos < strlen(hex); pos += 2) {
                unsigned byte;
                assert(sscanf(hex + pos, "%2x", &byte) == 1);
                assert(byte == raw[offset++]);
            }
        }
        assert(offset == length);
    }
    std::cout << "Sniffer framing: CRC golden vector, every byte, boundaries and max metadata pass\n";
}
