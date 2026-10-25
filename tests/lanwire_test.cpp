// LANWire::text converts lobby strings between 32-bit (macOS/Linux wchar_t) and 16-bit (wire) characters.
#include "GameNetwork/LANWire.h"
#include <cassert>
#include <cstdio>

int main()
{
	// ASCII and BMP round trip
	const wchar_t *name = L"Genéral 将军";
	uint16_t wire[32];
	LANWire::text(wire, 32, name, 32);
	assert(wire[0] == 'G' && wire[3] == 0xE9 && wire[8] == 0x5C06);
	wchar_t back[32];
	LANWire::text(back, 32, wire, 32);
	for (int i = 0; ; ++i) { assert(back[i] == name[i]); if (!name[i]) break; }

	// a character outside the BMP is a surrogate pair on the wire
	const uint32_t face[] = { 'a', 0x1F600, 'b', 0 };
	LANWire::text(wire, 32, face, 4);
	assert(wire[0] == 'a' && wire[1] == 0xD83D && wire[2] == 0xDE00 && wire[3] == 'b' && wire[4] == 0);
	uint32_t wide[8];
	LANWire::text(wide, 8, wire, 32);
	assert(wide[1] == 0x1F600 && wide[2] == 'b' && wide[3] == 0);

	// truncation: always terminated, and a pair is never cut in half
	uint16_t small[3];
	LANWire::text(small, 3, face, 4); // room for 'a' + terminator only: the pair needs 2 more
	assert(small[0] == 'a' && small[1] == 0);
	uint16_t cap4[4];
	LANWire::text(cap4, 4, face, 4);  // 'a' + pair fit, then the terminator
	assert(cap4[0] == 'a' && cap4[1] == 0xD83D && cap4[2] == 0xDE00 && cap4[3] == 0);

	// source without a terminator stops at its capacity
	const uint32_t full[] = { 'x', 'y', 'z' };
	uint16_t out[8];
	LANWire::text(out, 8, full, 3);
	assert(out[2] == 'z' && out[3] == 0);

	// a lone surrogate passes through unchanged (no crash)
	const uint16_t lone[] = { 0xD83D, 'q', 0 };
	uint16_t same[4];
	LANWire::text(same, 4, lone, 3);
	assert(same[0] == 0xD83D && same[1] == 'q' && same[2] == 0);
	puts("lanwire_test ok");
}
