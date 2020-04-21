#include "util.hpp"

namespace Helix::util {
	char nibbleToChar (std::byte value) {
        if (value <= std::byte(9)) {
            return '0' + static_cast<char>(value);
        } else {
            return 'A' + (static_cast<char>(value) - 10);
        }
    }

	std::pair<char, char> byteToString (std::byte value, bool padded) {
        return {
            nibbleToChar((value & std::byte(0xF0)) >> 4),
            nibbleToChar((value & std::byte(0x0F)))
        };
    }
    std::pair<char, char> byteToString (std::byte value) {
        return byteToString(value, false);
    }
}