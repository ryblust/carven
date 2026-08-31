module carven:backend.target.ids;

import :support.typed_id;
import std;

struct TargetTypeIDTag final {};
struct TargetExprIDTag final {};
struct TargetStmtIDTag final {};
struct TargetItemIDTag final {};

using TargetTypeID = TypedID<TargetTypeIDTag>;
using TargetExprID = TypedID<TargetExprIDTag>;
using TargetStmtID = TypedID<TargetStmtIDTag>;
using TargetItemID = TypedID<TargetItemIDTag>;
