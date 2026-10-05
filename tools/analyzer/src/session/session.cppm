module carven:analyzer.session;

import :editor.analysis;
import :editor.semantic;
import std;

struct AnalyzerUpdate final {
    std::string document;
    std::int64_t version;
    std::string text;
};

struct AnalyzerClose final {
    std::string document;
};

struct AnalyzerProjectModule final {
    std::string document;
    std::string module_path;
};

struct AnalyzerReplaceProject final {
    std::vector<AnalyzerProjectModule> modules;
};

struct AnalyzerCheck final {};

struct AnalyzerHover final {
    std::string document;
    std::uint32_t offset;
};

struct AnalyzerDefinition final {
    std::string document;
    std::uint32_t offset;
};

struct AnalyzerReferences final {
    std::string document;
    std::uint32_t offset;
};

struct AnalyzerStop final {};

using AnalyzerRequest = std::variant<
    AnalyzerUpdate,
    AnalyzerClose,
    AnalyzerReplaceProject,
    AnalyzerCheck,
    AnalyzerHover,
    AnalyzerDefinition,
    AnalyzerReferences,
    AnalyzerStop>;

struct AnalyzerFailure final {
    std::string code;
    std::string message;
};

struct AnalyzerAcknowledgement final {};

struct AnalyzerDiagnosticLabel final {
    EditorVersionedLocation location;
    std::string message;
};

struct AnalyzerDiagnosticNote final {
    std::optional<EditorVersionedLocation> location;
    std::string message;
};

struct AnalyzerDiagnostic final {
    std::string severity;
    std::string code;
    std::string message;
    std::optional<AnalyzerDiagnosticLabel> primary;
    std::vector<AnalyzerDiagnosticLabel> related;
    std::vector<AnalyzerDiagnosticNote> notes;
    std::vector<std::string> helps;
};

struct AnalyzerOutput final {
    std::string stream;
    std::string bytes;
};

struct AnalyzerCheckResult final {
    bool published;
    std::vector<AnalyzerDiagnostic> diagnostics;
    std::vector<AnalyzerOutput> output;
};

struct AnalyzerHoverInformation final {
    EditorVersionedLocation location;
    std::string type_text;
};

struct AnalyzerHoverResult final {
    std::optional<AnalyzerHoverInformation> information;
};

struct AnalyzerDefinitionResult final {
    std::optional<EditorVersionedLocation> location;
};

struct AnalyzerReferencesResult final {
    std::optional<std::vector<EditorVersionedLocation>> locations;
};

using AnalyzerResult = std::variant<
    AnalyzerFailure,
    AnalyzerAcknowledgement,
    AnalyzerCheckResult,
    AnalyzerHoverResult,
    AnalyzerDefinitionResult,
    AnalyzerReferencesResult>;

// Owns all response data. No compiler identities or borrows leave the session.
struct AnalyzerResponse final {
    std::vector<EditorDocumentVersion> document_versions;
    AnalyzerResult result;
};

// Requests execute serially against the current inputs. A query's document
// versions cover every available input in the explicit selected project.
class AnalyzerSession final {
public:
    auto execute(AnalyzerRequest request) noexcept -> AnalyzerResponse;
    auto counts() const noexcept -> EditorQueryCounts;

private:
    EditorAnalysisHost host;
    std::vector<EditorProjectModule> project;
};
