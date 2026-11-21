// G0 --play harness: the pure script parser / assertion evaluator / hash.
#include "doctest.h"
#include "play_script.hpp"

using namespace ra2engine::play;

TEST_CASE("play_script: parse commands, comments and blank lines") {
    std::vector<Command> cs;
    std::string err;
    REQUIRE(parse_script("# header\n\nwait 3\nclick button Single Player  # go\n"
                         "key Escape ctrl\nassert scene.game == 1\nuntil 30 sel >= 1\nend\n",
                         cs, err));
    REQUIRE(cs.size() == 6);
    CHECK(cs[0].op == "wait");
    CHECK(cs[0].line == 3);
    CHECK(cs[1].args == std::vector<std::string>{"button", "Single", "Player"});
    CHECK(cs[2].args == std::vector<std::string>{"Escape", "ctrl"});
    CHECK(cs[4].op == "until");
    CHECK(cs[5].op == "end");
}

TEST_CASE("play_script: unknown op and malformed args are errors") {
    std::vector<Command> cs;
    std::string err;
    CHECK_FALSE(parse_script("wiat 3\n", cs, err));
    CHECK(err.find("line 1") != std::string::npos);
    CHECK_FALSE(parse_script("wait x\n", cs, err));
    CHECK_FALSE(parse_script("assert a ==\n", cs, err));
    CHECK_FALSE(parse_script("until a b == 1\n", cs, err));
    CHECK_FALSE(parse_script("click\n", cs, err));
}

TEST_CASE("play_script: eval_assert operators and missing keys") {
    Probe p{{"credits", 10000}, {"sel", 2}};
    bool r = false;
    std::string err;
    REQUIRE(eval_assert({"credits", "==", "10000"}, p, r, err));
    CHECK(r);
    REQUIRE(eval_assert({"sel", ">", "2"}, p, r, err));
    CHECK_FALSE(r);
    REQUIRE(eval_assert({"sel", "<=", "2"}, p, r, err));
    CHECK(r);
    REQUIRE(eval_assert({"missing", "==", "0"}, p, r, err));
    CHECK(r);
    CHECK_FALSE(eval_assert({"sel", "~", "2"}, p, r, err));
    CHECK_FALSE(eval_assert({"sel", "==", "x"}, p, r, err));
}

TEST_CASE("play_script: probe_line is ordered and hash is stable") {
    Probe p{{"b", 2}, {"a", 1}};
    CHECK(probe_line(p) == "a=1 b=2");
    CHECK(fnv1a("") == 1469598103934665603ULL);
    CHECK(fnv1a("a=1 b=2") == fnv1a("a=1 b=2"));
    CHECK(fnv1a("a=1 b=2") != fnv1a("a=1 b=3"));
}

TEST_CASE("play_script: unit selector") {
    int h = -1, n = -1;
    std::string img;
    REQUIRE(parse_unit_selector("0:GI", h, img, n));
    CHECK(h == 0);
    CHECK(img == "GI");
    CHECK(n == 0);
    REQUIRE(parse_unit_selector("1:MTNK:3", h, img, n));
    CHECK(h == 1);
    CHECK(n == 3);
    CHECK_FALSE(parse_unit_selector("GI", h, img, n));
    CHECK_FALSE(parse_unit_selector("0:GI:x", h, img, n));
}
