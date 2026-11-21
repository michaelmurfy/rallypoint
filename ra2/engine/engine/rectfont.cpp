#include "rectfont.hpp"
// 5x7 rect-font glyph table [RE: skirmish.md §UI, D5]. Same technique as the
// kDigits strip in engine/main.cpp: 7 rows per glyph, low 5 bits used with
// bit 4 = leftmost column. PURE (no SDL); host-tested. NOT hashed -- consumed
// only by draw_text for menu labels, which no byte-identity gate calls.
namespace ra2engine {
namespace {
constexpr std::array<std::uint8_t, 7> kBlank{0, 0, 0, 0, 0, 0, 0};

// A-Z, 5 wide x 7 tall (bit 4 = leftmost column). Hand-authored.
constexpr std::array<std::uint8_t, 7> kA{0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
constexpr std::array<std::uint8_t, 7> kB{0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110};
constexpr std::array<std::uint8_t, 7> kC{0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110};
constexpr std::array<std::uint8_t, 7> kD{0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100};
constexpr std::array<std::uint8_t, 7> kE{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111};
constexpr std::array<std::uint8_t, 7> kF{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000};
constexpr std::array<std::uint8_t, 7> kG{0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111};
constexpr std::array<std::uint8_t, 7> kH{0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
constexpr std::array<std::uint8_t, 7> kI{0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110};
constexpr std::array<std::uint8_t, 7> kJ{0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100};
constexpr std::array<std::uint8_t, 7> kK{0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001};
constexpr std::array<std::uint8_t, 7> kL{0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111};
constexpr std::array<std::uint8_t, 7> kM{0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001};
constexpr std::array<std::uint8_t, 7> kN{0b10001, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001};
constexpr std::array<std::uint8_t, 7> kO{0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
constexpr std::array<std::uint8_t, 7> kP{0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000};
constexpr std::array<std::uint8_t, 7> kQ{0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101};
constexpr std::array<std::uint8_t, 7> kR{0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001};
constexpr std::array<std::uint8_t, 7> kS{0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110};
constexpr std::array<std::uint8_t, 7> kT{0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100};
constexpr std::array<std::uint8_t, 7> kU{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
constexpr std::array<std::uint8_t, 7> kV{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100};
constexpr std::array<std::uint8_t, 7> kW{0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001};
constexpr std::array<std::uint8_t, 7> kX{0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001};
constexpr std::array<std::uint8_t, 7> kY{0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100};
constexpr std::array<std::uint8_t, 7> kZ{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111};

// 0-9: exact kDigits bit patterns (engine/main.cpp:2517) for cross-consistency.
constexpr std::array<std::uint8_t, 7> k0{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
constexpr std::array<std::uint8_t, 7> k1{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
constexpr std::array<std::uint8_t, 7> k2{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
constexpr std::array<std::uint8_t, 7> k3{0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E};
constexpr std::array<std::uint8_t, 7> k4{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
constexpr std::array<std::uint8_t, 7> k5{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
constexpr std::array<std::uint8_t, 7> k6{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
constexpr std::array<std::uint8_t, 7> k7{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
constexpr std::array<std::uint8_t, 7> k8{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
constexpr std::array<std::uint8_t, 7> k9{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};

// Basic punctuation.
constexpr std::array<std::uint8_t, 7> kColon{0, 0b00100, 0, 0, 0b00100, 0, 0};
constexpr std::array<std::uint8_t, 7> kDash{0, 0, 0, 0b11111, 0, 0, 0};
constexpr std::array<std::uint8_t, 7> kDot{0, 0, 0, 0, 0, 0, 0b00100};
// W2-AR-03: path / message punctuation for the missing-files and error pages.
constexpr std::array<std::uint8_t, 7> kSlash{0b00001, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b10000};
constexpr std::array<std::uint8_t, 7> kBackslash{0b10000, 0b10000, 0b01000, 0b00100, 0b00010, 0b00001, 0b00001};
constexpr std::array<std::uint8_t, 7> kUnderscore{0, 0, 0, 0, 0, 0, 0b11111};
constexpr std::array<std::uint8_t, 7> kLParen{0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100, 0b00010};
constexpr std::array<std::uint8_t, 7> kRParen{0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100, 0b01000};
constexpr std::array<std::uint8_t, 7> kComma{0, 0, 0, 0, 0, 0b00100, 0b01000};
constexpr std::array<std::uint8_t, 7> kApos{0b00100, 0b00100, 0b01000, 0, 0, 0, 0};
constexpr std::array<std::uint8_t, 7> kExcl{0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0, 0b00100};
constexpr std::array<std::uint8_t, 7> kQuest{0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0, 0b00100};
constexpr std::array<std::uint8_t, 7> kPlus{0, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0};
constexpr std::array<std::uint8_t, 7> kPercent{0b11001, 0b11001, 0b00010, 0b00100, 0b01000, 0b10011, 0b10011};
constexpr std::array<std::uint8_t, 7> kLBracket{0b01110, 0b01000, 0b01000, 0b01000, 0b01000, 0b01000, 0b01110};
constexpr std::array<std::uint8_t, 7> kRBracket{0b01110, 0b00010, 0b00010, 0b00010, 0b00010, 0b00010, 0b01110};
constexpr std::array<std::uint8_t, 7> kLess{0b00010, 0b00100, 0b01000, 0b10000, 0b01000, 0b00100, 0b00010};
constexpr std::array<std::uint8_t, 7> kGreater{0b01000, 0b00100, 0b00010, 0b00001, 0b00010, 0b00100, 0b01000};
constexpr std::array<std::uint8_t, 7> kEquals{0, 0, 0b11111, 0, 0b11111, 0, 0};
}  // namespace

const std::array<std::uint8_t, 7>& glyph_rows(char c) {
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    switch (c) {
        case 'A': return kA;
        case 'B': return kB;
        case 'C': return kC;
        case 'D': return kD;
        case 'E': return kE;
        case 'F': return kF;
        case 'G': return kG;
        case 'H': return kH;
        case 'I': return kI;
        case 'J': return kJ;
        case 'K': return kK;
        case 'L': return kL;
        case 'M': return kM;
        case 'N': return kN;
        case 'O': return kO;
        case 'P': return kP;
        case 'Q': return kQ;
        case 'R': return kR;
        case 'S': return kS;
        case 'T': return kT;
        case 'U': return kU;
        case 'V': return kV;
        case 'W': return kW;
        case 'X': return kX;
        case 'Y': return kY;
        case 'Z': return kZ;
        case '0': return k0;
        case '1': return k1;
        case '2': return k2;
        case '3': return k3;
        case '4': return k4;
        case '5': return k5;
        case '6': return k6;
        case '7': return k7;
        case '8': return k8;
        case '9': return k9;
        case ':': return kColon;
        case '-': return kDash;
        case '.': return kDot;
        case '/': return kSlash;
        case '\\': return kBackslash;
        case '_': return kUnderscore;
        case '(': return kLParen;
        case ')': return kRParen;
        case ',': return kComma;
        case '\'': return kApos;
        case '!': return kExcl;
        case '?': return kQuest;
        case '+': return kPlus;
        case '%': return kPercent;
        case '[': return kLBracket;
        case ']': return kRBracket;
        case '<': return kLess;
        case '>': return kGreater;
        case '=': return kEquals;
        case ' ':
        default: return kBlank;
    }
}
}  // namespace ra2engine
