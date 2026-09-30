module carven:semantic.semir.content;

import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.type;
import std;

// Content keys serialize canonical structure and source declaration identities,
// never insertion positions. Repeated structure uses local node references.
// Closure identities use their body's source origin. Dependency depth does not
// grow the native traversal stack.
auto type_content_key(const SemIRProgram& program, TypeID type) noexcept -> std::string;
auto constant_content_key(const SemIRProgram& program, ConstantID constant) noexcept -> std::string;
