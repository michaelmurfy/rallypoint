// Checks RelayWire.h against the Rust protocol's golden bytes (shared/src/lib.rs `wire_layout`).
#include "GameNetwork/RelayWire.h"
#include <cassert>

int main()
{
	using namespace RelayWire;
	uint8_t out[64];
	const uint8_t payload[] = { 0xAA };

	// Data to 10.77.0.1 from game port 8088 to 8086: same bytes as Packet::Data in Rust.
	size_t n = encode(out, sizeof(out), (10u << 24) | (77u << 16) | 1u, 8088, 8086, payload, 1);
	const uint8_t golden[] = { 5, 10, 77, 0, 1, 0x1F, 0x98, 0x1F, 0x96, 0xAA };
	assert(n == sizeof(golden) && memcmp(out, golden, n) == 0);

	// Broadcast destination becomes a Bcast with zero vip.
	n = encode(out, sizeof(out), 0xFFFFFFFFu, 8086, 8086, payload, 1);
	assert(out[0] == 4 && out[1] == 0 && out[4] == 0 && n == 10);

	// Bind matches Packet::Bind.
	n = encodeBind(out, 8086);
	assert(n == 3 && out[0] == 7 && out[1] == 0x1F && out[2] == 0x96);

	// Decode what the agent sends back.
	uint32_t ip; uint16_t sp, dp; const uint8_t *p; size_t pl;
	assert(decode(golden, sizeof(golden), ip, sp, dp, p, pl));
	assert(ip == ((10u << 24) | (77u << 16) | 1u) && sp == 8088 && dp == 8086 && pl == 1 && p[0] == 0xAA);
	assert(!decode(golden, 8, ip, sp, dp, p, pl));      // short
	const uint8_t welcome[] = { 2, 10, 77, 0, 1 };
	assert(!decode(welcome, sizeof(welcome), ip, sp, dp, p, pl)); // not data
	assert(!encode(out, 5, 1, 1, 1, payload, 1));        // cap too small

	// Env parsing.
	Config c = parseConfig("127.0.0.1:47770", "10.77.0.2");
	assert(c.on && c.agentIp == 0x7F000001 && c.agentPort == 47770 && c.vip == ((10u << 24) | (77u << 16) | 2u));
	assert(!parseConfig(nullptr, "10.77.0.2").on);
	assert(!parseConfig("127.0.0.1:0", "10.77.0.2").on);
	assert(!parseConfig("127.0.0.1:5", "10.77.0.999").on);
	return 0;
}
