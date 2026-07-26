module carven:source.provenance.ids;

import :support.typed_id;

struct ProgramSourceIDTag final {};
struct ProgramModuleIDTag final {};
struct ProgramSpellingIDTag final {};
struct ProgramOriginIDTag final {};

using ProgramSourceID = TypedID<ProgramSourceIDTag>;
using ProgramModuleID = TypedID<ProgramModuleIDTag>;
using ProgramSpellingID = TypedID<ProgramSpellingIDTag>;
using ProgramOriginID = TypedID<ProgramOriginIDTag>;
