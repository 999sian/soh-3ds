#pragma once
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "parser.h"
struct Program {
    std::vector<uint16_t> words;
    unsigned instructions = 0;
    std::vector<unsigned> relocations; // expansion words containing program addresses
    void Emit(std::string text, uint16_t expansion = 0) {
        std::istringstream stream(text); std::vector<std::string> tokens; std::string token;
        while (stream >> token) tokens.push_back(token);
        // Parser lookup is read-only; build its 65,536-opcode trie once per process.
        static auto parser = Teakra::GenerateParser();
        auto opcode = parser->Parse(tokens);
        if (opcode.status == Teakra::Parser::Opcode::Invalid) throw std::runtime_error("Cannot assemble: " + text);
        words.push_back(opcode.opcode);
        if (opcode.status == Teakra::Parser::Opcode::ValidWithExpansion) words.push_back(expansion);
        ++instructions;
    }
};
inline Program MakeProgram(bool subtract, bool block = false) {
    Program p;
    if (block) p.Emit("bkrep r6 0x00000000");
    if (subtract) {
        p.Emit("mov [r1] a0");
        p.Emit("sub [r0++] a0");
        p.Emit("shfi a0 a0 +0x0010");
        p.Emit("mov a0h [r1++]");
    } else {
        p.Emit("mov r2 y0");
        p.Emit("mpy y0 [r0++] a0");
        p.Emit("mov p* a0");
        p.Emit("mov 0x0000 y0", 0x7fff);
        p.Emit("mpy y0 [r1] a1");
        p.Emit("add p* a0");
        p.Emit("add 0x0000 a0", 0x4000);
        // Doubled Q15 result becomes a Q16 value. Saturating high-word store
        // implements signed16 clipping and the original arithmetic shift.
        p.Emit("shfi a0 a0 +0x0001");
        p.Emit("mov a0h [r1++]");
    }
    if (block) p.words[1] = uint16_t(p.words.size() - 1);
    return p;
}
