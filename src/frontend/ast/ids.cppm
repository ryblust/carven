module carven:frontend.ast.ids;

import :support.typed_id;

struct ASTExprIDTag final {};
struct ASTTypeIDTag final {};
struct ASTStmtIDTag final {};
struct ASTPatternIDTag final {};
struct ASTBlockIDTag final {};
struct ASTBranchBlockIDTag final {};
struct ASTItemIDTag final {};
struct ASTImportIDTag final {};

using ASTExprID = TypedID<ASTExprIDTag>;
using ASTTypeID = TypedID<ASTTypeIDTag>;
using ASTStmtID = TypedID<ASTStmtIDTag>;
using ASTPatternID = TypedID<ASTPatternIDTag>;
using ASTBlockID = TypedID<ASTBlockIDTag>;
using ASTBranchBlockID = TypedID<ASTBranchBlockIDTag>;
using ASTItemID = TypedID<ASTItemIDTag>;
using ASTImportID = TypedID<ASTImportIDTag>;
