module carven:frontend.ast.imports.impl;

import :frontend.ast.imports;
import :source.text;

auto ASTImportItem::local_name_span() const noexcept -> Span {
    return alias_span.value_or(name_span);
}
