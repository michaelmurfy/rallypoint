#include "doctest.h"
#include "play_script.hpp"

using namespace ra2play;

TEST_CASE("play_lang: parses commands, tags, expressions") {
    Script s;
    std::string err;
    const char* text =
        "# header comment\n"
        "click button \"Single Player\"   # trailing comment\n"
        "wait 30\n"
        "until 1800 credits > 10000 @G-20:A\n"
        "assert units(0,E1) >= 1 and scene == game @G-22:A\n"
        "let c0 = credits\n"
        "key esc @G-01:B\n"
        "hold left 10\n"
        "click cell 50 60 right shift\n"
        "end expect WON\n";
    REQUIRE(parse_script(text, s, err));
    REQUIRE(s.cmds.size() == 9);
    CHECK(s.cmds[0].op == "click");
    REQUIRE(s.cmds[0].args.size() == 2);
    CHECK(s.cmds[0].args[1] == "Single Player");
    CHECK(s.cmds[1].n == 30);
    CHECK(s.cmds[2].op == "until");
    CHECK(s.cmds[2].n == 1800);
    CHECK(s.cmds[2].expr == "credits > 10000");
    REQUIRE(s.cmds[2].tags.size() == 1);
    CHECK(s.cmds[2].tags[0] == "G-20:A");
    CHECK(s.cmds[3].expr == "units(0,E1) >= 1 and scene == game");
    CHECK(s.cmds[4].op == "let");
    CHECK(s.cmds[4].expr == "credits");
    CHECK(s.cmds[6].n == 10);
    CHECK(s.cmds[8].line == 10);
}

TEST_CASE("play_lang: unknown op and malformed lines are errors") {
    Script s;
    std::string err;
    CHECK_FALSE(parse_script("clik button X\n", s, err));
    CHECK(err.find("line 1") != std::string::npos);
    CHECK_FALSE(parse_script("wait\n", s, err));
    CHECK_FALSE(parse_script("until abc credits > 1\n", s, err));
    CHECK_FALSE(parse_script("let x credits\n", s, err));
}

TEST_CASE("play_lang: expression evaluation, families, missing keys") {
    ProbeData p;
    p.set("credits", 10000);
    p.set("units(0,E1)", 3);
    p.set_s("scene", "game");
    p.zero_families.insert("units");
    std::map<std::string, Value> vars;
    std::string d;
    CHECK(eval_expr("credits == 10000", p, vars, d) == Eval::True);
    CHECK(eval_expr("credits > 10000", p, vars, d) == Eval::False);
    CHECK(d.find("credits=10000") != std::string::npos);
    CHECK(eval_expr("units(0,E1) >= 3", p, vars, d) == Eval::True);
    CHECK(eval_expr("units(1,E2) == 0", p, vars, d) == Eval::True);       // zero family
    CHECK(eval_expr("buildings(0) > 0", p, vars, d) == Eval::Missing);    // not a family
    CHECK(d == "missing-probe:buildings(0)");
    CHECK(eval_expr("sel.moving == 0", p, vars, d) == Eval::True);        // B's dotted keys: absent = 0
    CHECK(eval_expr("scene == game", p, vars, d) == Eval::True);          // bare RHS literal
    CHECK(eval_expr("scene == \"game\"", p, vars, d) == Eval::True);
    CHECK(eval_expr("scene != shell", p, vars, d) == Eval::True);
    CHECK(eval_expr("credits > 1 and scene == game", p, vars, d) == Eval::True);
    CHECK(eval_expr("credits > 99999 or scene == game", p, vars, d) == Eval::True);
    CHECK(eval_expr("credits > 99999 and scene == game", p, vars, d) == Eval::False);
    CHECK(eval_expr("credits", p, vars, d) == Eval::True);
    CHECK(eval_expr("credits >", p, vars, d) == Eval::Error);
    Value v;
    REQUIRE(resolve_operand("credits", p, vars, true, v));
    vars["c0"] = v;
    p.set("credits", 10500);
    CHECK(eval_expr("credits > $c0", p, vars, d) == Eval::True);
    CHECK(eval_expr("credits > $nope", p, vars, d) == Eval::Missing);
    CHECK(eval_expr("credits == $c0+500", p, vars, d) == Eval::True);
    CHECK(eval_expr("credits > $c0-1", p, vars, d) == Eval::True);
}

TEST_CASE("play_lang: probe hash is order-independent and value-sensitive") {
    ProbeData a, b;
    a.set("x", 1);
    a.set("y", 2);
    b.set("y", 2);
    b.set("x", 1);
    CHECK(probe_hash(a, 7) == probe_hash(b, 7));
    b.set("y", 3);
    CHECK(probe_hash(a, 7) != probe_hash(b, 7));
}
