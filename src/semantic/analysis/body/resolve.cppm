module carven:semantic.analysis.body.resolve;

import :semantic.semir.program;
import :semantic.semir.structured;

auto resolve_body(StructuredBodyDraft&& body, ProgramDraft& draft) noexcept -> SemIRBody;
