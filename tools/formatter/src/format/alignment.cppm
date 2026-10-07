module carven:formatter.format.alignment;

import :formatter.source;
import :frontend.ast.tree;
import std;

// Adds column padding to eligible adjacent array rows after ordinary layout.
// The source and syntax are borrowed only for this call.
auto align_formatted_array_rows(
    const FormattingSource& source,
    ASTView syntax,
    std::string output,
    std::size_t width
) noexcept -> std::string;
