// engine/mp/lobby_wire.cpp -- see lobby_wire.hpp.
#include "mp/lobby_wire.hpp"

#include "net/global_msg.hpp"
#include "net/wire.hpp"

#include <algorithm>
#include <cstring>

namespace ra2mp {

void BodyWriter::fixed(const std::string& s, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) u8(i + 1 < width && i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
}

std::string BodyReader::fixed(std::size_t width) {
    std::string s;
    bool end = false;
    for (std::size_t i = 0; i < width; ++i) {
        const std::uint8_t c = u8();
        if (c == 0) end = true;
        if (!end) s += static_cast<char>(c);
    }
    return s;
}

std::uint32_t game_name_hash(const std::string& name) {
    return ra2::net::packet_crc(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());
}

// The original chat packet is engine/net's (global_msg.hpp); the lobby keeps
// its UTF-8 view of it.
std::vector<std::uint8_t> encode_chat(const ChatPacket& c) {
    ra2::net::ChatMessage m;
    m.sender = c.sender.substr(0, 0x2A);
    m.chat_mask = c.chat_mask;
    m.text = ra2::net::utf8_to_u16(c.text);
    m.color = c.color;
    m.game_hash = c.game_hash;
    return ra2::net::encode_chat(m);
}

bool decode_chat(const std::vector<std::uint8_t>& body, ChatPacket& out) {
    ra2::net::ChatMessage m;
    if (!ra2::net::decode_chat(body, m)) return false;
    out.sender = m.sender;
    out.chat_mask = m.chat_mask;
    out.text = ra2::net::u16_to_utf8(m.text);
    out.color = m.color;
    out.game_hash = m.game_hash;
    return true;
}

}  // namespace ra2mp
