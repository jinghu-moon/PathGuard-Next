#include "pathguard/rules/arrow_scanner.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "pathguard/rules/format_probe.h"

namespace pathguard::rules {
namespace {

enum class SignificantKind : std::uint8_t {
    kString,
    kMultilineString,
    kArrow,
    kOpenArray,
    kCloseArray,
    kOpenInlineTable,
    kCloseInlineTable,
    kComma,
    kEquals,
    kDot,
    kOther,
};

enum class FrameKind : std::uint8_t {
    kValueArray,
    kInlineTable,
    kTableHeader,
    kArrayTableHeader,
    kUnknownBracket,
    kUnknownBrace,
};

struct SignificantToken {
    SignificantKind kind;
    ByteSpan span;
    std::uint32_t frame_id;
};

struct Frame {
    FrameKind kind;
    std::uint32_t id;
    char closing;
    std::uint8_t closing_count;
};

enum class PendingStage : std::uint8_t {
    kWaitingTarget,
    kWaitingDelimiter,
};

struct PendingCandidate {
    PendingStage stage;
    ByteSpan source;
    ByteSpan arrow;
    ByteSpan target;
    std::uint32_t array_frame_id;
};

struct StringToken {
    std::size_t end;
    bool multiline;
    bool terminated;
};

bool IsHorizontalSpace(char value) {
    return value == ' ' || value == '\t';
}

bool IsNewline(char value) {
    return value == '\r' || value == '\n';
}

bool IsTokenBoundary(char value) {
    return IsHorizontalSpace(value) || IsNewline(value) || value == '#'
        || value == '"' || value == '\'' || value == '[' || value == ']'
        || value == '{' || value == '}' || value == '=' || value == ','
        || value == '.';
}

std::size_t QuoteRun(std::string_view bytes, std::size_t offset, char quote) {
    std::size_t end = offset;
    while (end < bytes.size() && bytes[end] == quote) ++end;
    return end - offset;
}

StringToken ScanStringToken(std::string_view bytes, std::size_t start) {
    const char quote = bytes[start];
    const bool basic = quote == '"';
    const bool multiline = start + 2 < bytes.size()
        && bytes[start + 1] == quote && bytes[start + 2] == quote;
    if (!multiline) {
        std::size_t slash_run = 0;
        for (std::size_t offset = start + 1; offset < bytes.size(); ++offset) {
            const char value = bytes[offset];
            if (IsNewline(value)) return {offset, false, false};
            if (value == quote && (!basic || slash_run % 2 == 0)) {
                return {offset + 1, false, true};
            }
            if (basic && value == '\\') ++slash_run;
            else slash_run = 0;
        }
        return {bytes.size(), false, false};
    }

    std::size_t slash_run = 0;
    for (std::size_t offset = start + 3; offset < bytes.size();) {
        if (bytes[offset] == quote) {
            const std::size_t run = QuoteRun(bytes, offset, quote);
            if (!basic && run >= 3) {
                return {offset + std::min<std::size_t>(run, 5), true, true};
            }
            if (basic && slash_run % 2 == 0 && run >= 3) {
                return {offset + std::min<std::size_t>(run, 5), true, true};
            }
            if (basic && slash_run % 2 == 1 && run >= 4) {
                return {offset + 1 + std::min<std::size_t>(run - 1, 5),
                        true, true};
            }
            slash_run = 0;
            offset += run;
            continue;
        }
        if (basic && bytes[offset] == '\\') ++slash_run;
        else slash_run = 0;
        ++offset;
    }
    return {bytes.size(), true, false};
}

class Scanner {
public:
    Scanner(const SourceBuffer& source, const RulesLimits& limits)
        : source_(source), bytes_(source.bytes()), limits_(limits) {
        frames_.reserve(std::min<std::size_t>(limits.max_container_depth, 256));
        result_.candidates.reserve(std::min<std::size_t>(limits.max_rewrites, 256));
        result_.diagnostics.reserve(std::min<std::size_t>(limits.max_diagnostics, 64));
    }

    ArrowScanResult Run() {
        std::size_t offset = 0;
        while (offset < bytes_.size() && !stopped_) {
            const char value = bytes_[offset];
            if (IsHorizontalSpace(value)) {
                ++offset;
                continue;
            }
            if (IsNewline(value)) {
                if (value == '\r' && offset + 1 < bytes_.size()
                    && bytes_[offset + 1] == '\n') {
                    offset += 2;
                } else {
                    ++offset;
                }
                if (frames_.empty()) {
                    at_statement_start_ = true;
                    expecting_value_ = false;
                }
                continue;
            }
            if (value == '#') {
                HandleComment(offset);
                while (offset < bytes_.size() && bytes_[offset] != '\n') ++offset;
                continue;
            }
            if (value == '"' || value == '\'') {
                const StringToken string = ScanStringToken(bytes_, offset);
                const std::size_t token_end = std::max(offset + 1, string.end);
                if (token_end - offset > limits_.max_string_token_bytes) {
                    AddDiagnostic(kResourceLimit, "rules.string_token_too_large",
                                  Span(offset, token_end));
                    stopped_ = true;
                    offset = token_end;
                    break;
                }
                if (!string.terminated) {
                    AddDiagnostic(kArrowStringBoundary,
                                  "rules.arrow_string_boundary",
                                  Span(offset, token_end));
                    stopped_ = true;
                    offset = token_end;
                    break;
                }
                Emit({string.multiline ? SignificantKind::kMultilineString
                                       : SignificantKind::kString,
                      Span(offset, string.end), CurrentFrameId()});
                expecting_value_ = false;
                at_statement_start_ = false;
                offset = string.end;
                continue;
            }
            if (value == '-' && offset + 1 < bytes_.size()
                && bytes_[offset + 1] == '>') {
                Emit({SignificantKind::kArrow, Span(offset, offset + 2),
                      CurrentFrameId()});
                offset += 2;
                continue;
            }
            if (value == '[') {
                offset = OpenArray(offset);
                continue;
            }
            if (value == ']') {
                offset = CloseArray(offset);
                continue;
            }
            if (value == '{') {
                OpenBrace(offset);
                ++offset;
                continue;
            }
            if (value == '}') {
                CloseBrace(offset);
                ++offset;
                continue;
            }
            if (value == '=') {
                Emit({SignificantKind::kEquals, Span(offset, offset + 1),
                      CurrentFrameId()});
                expecting_value_ = true;
                at_statement_start_ = false;
                ++offset;
                continue;
            }
            if (value == ',') {
                const std::uint32_t frame_id = CurrentFrameId();
                Emit({SignificantKind::kComma, Span(offset, offset + 1), frame_id});
                if (CurrentFrameKind() == FrameKind::kValueArray) {
                    expecting_value_ = true;
                } else {
                    expecting_value_ = false;
                }
                ++offset;
                continue;
            }
            if (value == '.') {
                Emit({SignificantKind::kDot, Span(offset, offset + 1),
                      CurrentFrameId()});
                expecting_value_ = false;
                at_statement_start_ = false;
                ++offset;
                continue;
            }

            std::size_t end = offset + 1;
            while (end < bytes_.size() && !IsTokenBoundary(bytes_[end])
                   && !(bytes_[end] == '-' && end + 1 < bytes_.size()
                        && bytes_[end + 1] == '>')) {
                ++end;
            }
            Emit({SignificantKind::kOther, Span(offset, end), CurrentFrameId()});
            expecting_value_ = false;
            at_statement_start_ = false;
            offset = end;
        }
        result_.bytes_consumed = static_cast<std::uint32_t>(offset);
        result_.open_frames_at_eof = static_cast<std::uint32_t>(frames_.size());
        FinishAtEof();
        if (has_error_) result_.candidates.clear();
        return std::move(result_);
    }

private:
    ByteSpan Span(std::size_t begin, std::size_t end) const {
        return {static_cast<std::uint32_t>(begin),
                static_cast<std::uint32_t>(end)};
    }

    std::uint32_t CurrentFrameId() const {
        return frames_.empty() ? 0 : frames_.back().id;
    }

    FrameKind CurrentFrameKind() const {
        return frames_.empty() ? FrameKind::kUnknownBracket : frames_.back().kind;
    }

    bool IsValueArrayFrame(std::uint32_t id) const {
        return std::any_of(frames_.begin(), frames_.end(), [id](const Frame& frame) {
            return frame.id == id && frame.kind == FrameKind::kValueArray;
        });
    }

    void AddDiagnostic(std::string_view code, std::string_view message_key,
                       ByteSpan primary) {
        has_error_ = true;
        const std::size_t limit = std::max<std::size_t>(limits_.max_diagnostics, 1);
        if (result_.diagnostics.size() < limit) {
            result_.diagnostics.push_back({code, message_key, primary, false});
            return;
        }
        if (!omitted_) {
            result_.diagnostics.back() = {
                kDiagnosticsOmitted, "rules.diagnostics_omitted", primary, true};
            omitted_ = true;
        }
    }

    bool PushFrame(FrameKind kind, char closing, std::uint8_t closing_count,
                   std::size_t offset) {
        if (frames_.size() >= limits_.max_container_depth) {
            AddDiagnostic(kResourceLimit, "rules.container_depth",
                          Span(offset, offset + 1));
            stopped_ = true;
            return false;
        }
        frames_.push_back({kind, next_frame_id_++, closing, closing_count});
        result_.max_frame_depth = std::max(
            result_.max_frame_depth, static_cast<std::uint32_t>(frames_.size()));
        return true;
    }

    std::size_t OpenArray(std::size_t offset) {
        const bool array_table = frames_.empty() && at_statement_start_
            && !expecting_value_ && offset + 1 < bytes_.size()
            && bytes_[offset + 1] == '[';
        FrameKind kind = FrameKind::kUnknownBracket;
        std::uint8_t width = 1;
        if (array_table) {
            kind = FrameKind::kArrayTableHeader;
            width = 2;
        } else if (frames_.empty() && at_statement_start_ && !expecting_value_) {
            kind = FrameKind::kTableHeader;
        } else if (expecting_value_) {
            kind = FrameKind::kValueArray;
        }
        if (!PushFrame(kind, ']', width, offset)) return bytes_.size();
        Emit({SignificantKind::kOpenArray, Span(offset, offset + width),
              CurrentFrameId()});
        expecting_value_ = kind == FrameKind::kValueArray;
        at_statement_start_ = false;
        return offset + width;
    }

    std::size_t CloseArray(std::size_t offset) {
        const std::uint32_t frame_id = CurrentFrameId();
        std::size_t width = 1;
        if (!frames_.empty() && frames_.back().closing == ']'
            && frames_.back().closing_count == 2
            && offset + 1 < bytes_.size() && bytes_[offset + 1] == ']') {
            width = 2;
        }
        Emit({SignificantKind::kCloseArray, Span(offset, offset + width), frame_id});
        if (!frames_.empty() && frames_.back().closing == ']') frames_.pop_back();
        expecting_value_ = false;
        return offset + width;
    }

    void OpenBrace(std::size_t offset) {
        const FrameKind kind = expecting_value_ ? FrameKind::kInlineTable
                                                : FrameKind::kUnknownBrace;
        if (!PushFrame(kind, '}', 1, offset)) return;
        Emit({SignificantKind::kOpenInlineTable, Span(offset, offset + 1),
              CurrentFrameId()});
        expecting_value_ = false;
        at_statement_start_ = false;
    }

    void CloseBrace(std::size_t offset) {
        const std::uint32_t frame_id = CurrentFrameId();
        Emit({SignificantKind::kCloseInlineTable, Span(offset, offset + 1), frame_id});
        if (!frames_.empty() && frames_.back().closing == '}') frames_.pop_back();
        expecting_value_ = false;
    }

    void HandleComment(std::size_t offset) {
        if (pending_ && pending_->stage == PendingStage::kWaitingTarget) {
            AddDiagnostic(kArrowCommentInside, "rules.arrow_comment_inside",
                          Span(offset, std::min(bytes_.size(), offset + 1)));
            pending_.reset();
        } else if (!pending_ || pending_->stage != PendingStage::kWaitingDelimiter) {
            comment_since_significant_ = true;
        }
    }

    void Emit(SignificantToken token) {
        if (stopped_) return;
        if (result_.significant_tokens >= limits_.max_tokens_or_nodes) {
            AddDiagnostic(kResourceLimit, "rules.token_limit", token.span);
            stopped_ = true;
            return;
        }
        ++result_.significant_tokens;
        HandlePending(token);
        previous_ = last_;
        last_ = token;
        comment_since_significant_ = false;
    }

    void HandlePending(const SignificantToken& token) {
        if (suppress_arrow_frame_ && token.kind == SignificantKind::kArrow
            && token.frame_id == *suppress_arrow_frame_) {
            return;
        }
        if (suppress_arrow_frame_
            && ((token.kind == SignificantKind::kComma
                 || token.kind == SignificantKind::kCloseArray)
                && token.frame_id == *suppress_arrow_frame_)) {
            suppress_arrow_frame_.reset();
        }

        if (pending_) {
            if (pending_->stage == PendingStage::kWaitingTarget) {
                if (token.kind == SignificantKind::kString
                    && token.frame_id == pending_->array_frame_id) {
                    pending_->target = token.span;
                    pending_->stage = PendingStage::kWaitingDelimiter;
                    return;
                }
                AddDiagnostic(kArrowOperand, "rules.arrow_operand", token.span);
                pending_.reset();
                return;
            }
            if ((token.kind == SignificantKind::kComma
                 || token.kind == SignificantKind::kCloseArray)
                && token.frame_id == pending_->array_frame_id) {
                ConfirmCandidate();
                return;
            }
            if (token.kind == SignificantKind::kArrow) {
                AddDiagnostic(kArrowChained, "rules.arrow_chained", token.span);
            } else if (token.kind == SignificantKind::kString
                       || token.kind == SignificantKind::kMultilineString) {
                AddDiagnostic(kArrowMissingComma, "rules.arrow_missing_comma",
                              token.span);
                suppress_arrow_frame_ = pending_->array_frame_id;
            } else {
                AddDiagnostic(kArrowContext, "rules.arrow_context", token.span);
            }
            pending_.reset();
            return;
        }

        if (token.kind != SignificantKind::kArrow) return;
        if (!last_ || last_->kind != SignificantKind::kString) {
            AddDiagnostic(kArrowOperand, "rules.arrow_operand", token.span);
            return;
        }
        if (comment_since_significant_) {
            AddDiagnostic(kArrowCommentInside, "rules.arrow_comment_inside",
                          token.span);
            return;
        }
        if (!previous_
            || (previous_->kind != SignificantKind::kOpenArray
                && previous_->kind != SignificantKind::kComma)
            || previous_->frame_id != last_->frame_id
            || last_->frame_id != token.frame_id
            || !IsValueArrayFrame(token.frame_id)) {
            AddDiagnostic(kArrowContext, "rules.arrow_context", token.span);
            return;
        }
        pending_ = PendingCandidate{PendingStage::kWaitingTarget,
                                    last_->span, token.span, {}, token.frame_id};
    }

    void ConfirmCandidate() {
        if (!pending_) return;
        if (result_.candidates.size() >= limits_.max_rewrites) {
            AddDiagnostic(kResourceLimit, "rules.rewrite_limit", pending_->arrow);
            pending_.reset();
            return;
        }
        result_.candidates.push_back({
            {pending_->source.begin, pending_->target.end},
            pending_->source,
            pending_->arrow,
            pending_->target,
            pending_->array_frame_id,
        });
        pending_.reset();
    }

    void FinishAtEof() {
        if (!pending_) return;
        if (pending_->stage == PendingStage::kWaitingTarget) {
            AddDiagnostic(kArrowOperand, "rules.arrow_operand", pending_->arrow);
            pending_.reset();
            return;
        }
        ConfirmCandidate();
    }

    const SourceBuffer& source_;
    std::string_view bytes_;
    const RulesLimits& limits_;
    ArrowScanResult result_;
    std::vector<Frame> frames_;
    std::optional<SignificantToken> previous_;
    std::optional<SignificantToken> last_;
    std::optional<PendingCandidate> pending_;
    std::optional<std::uint32_t> suppress_arrow_frame_;
    std::uint32_t next_frame_id_ = 1;
    bool at_statement_start_ = true;
    bool expecting_value_ = false;
    bool comment_since_significant_ = false;
    bool has_error_ = false;
    bool omitted_ = false;
    bool stopped_ = false;
};

}  // namespace

ArrowScanResult ScanArrowCandidates(const SourceBuffer& source,
                                    const RulesLimits& limits) {
    return Scanner(source, limits).Run();
}

RulesLexResult AnalyzeRulesSource(const SourceBuffer& source,
                                  const RulesLimits& limits) {
    RulesLexResult result;
    const FormatProbeResult format = ProbeRulesFormat(source);
    if (!format.ok()) {
        result.diagnostics.push_back(format.diagnostic);
        return result;
    }
    ArrowScanResult scanned = ScanArrowCandidates(source, limits);
    result.format_version = format.version;
    result.candidates = std::move(scanned.candidates);
    result.diagnostics = std::move(scanned.diagnostics);
    result.significant_tokens = scanned.significant_tokens;
    result.max_frame_depth = scanned.max_frame_depth;
    result.bytes_consumed = scanned.bytes_consumed;
    result.open_frames_at_eof = scanned.open_frames_at_eof;
    return result;
}

}  // namespace pathguard::rules
