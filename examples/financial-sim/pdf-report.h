#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// Minimal PDF Report Writer - Phase A.1 Completion
//
// Dependency-free writer that converts the audit trail structured-text export
// (and generic line-based reports) into a valid, self-contained PDF document.
//
// The writer implements a small, well-defined subset of the PDF 1.4 spec:
//   - Catalog, Pages, Page, Font (Helvetica), and Content stream objects
//   - Automatic pagination with configurable page size, margins, and font
//   - Monospace-friendly layout suitable for tabular audit/financial reports
//   - Correct xref table and byte offsets so the output opens in any viewer
//
// Design goals:
//   - Zero external dependencies (no libharu / poppler / cairo)
//   - Deterministic output for testability
//   - Safe text handling (escapes PDF string delimiters)
// ============================================================================

namespace ggnucash {
namespace report {

// ============================================================================
// PDF Document Configuration
// ============================================================================

struct PdfConfig {
    // Page geometry (PDF points; 1 point = 1/72 inch). Default = US Letter.
    double page_width;
    double page_height;

    // Margins in points.
    double margin_left;
    double margin_top;
    double margin_bottom;

    // Text layout.
    double font_size;       // points
    double line_height;     // points between successive baselines
    std::string font_name;  // one of the 14 standard PDF fonts

    // Optional metadata written into the document info dictionary.
    std::string title;
    std::string author;
    std::string creator;

    PdfConfig()
        : page_width(612.0),       // US Letter
          page_height(792.0),
          margin_left(54.0),       // 0.75 inch
          margin_top(54.0),
          margin_bottom(54.0),
          font_size(9.0),
          line_height(12.0),
          font_name("Courier"),    // monospace keeps tabular columns aligned
          title("GGNuCash Report"),
          author("GGNuCash"),
          creator("GGNuCash PDF Report Writer") {}

    // Convenience: ISO A4 page size.
    static PdfConfig a4() {
        PdfConfig cfg;
        cfg.page_width  = 595.0;
        cfg.page_height = 842.0;
        return cfg;
    }

    // Usable text area width in points.
    double text_width() const { return page_width - (2.0 * margin_left); }

    // Maximum lines that fit on one page given the configured geometry.
    int lines_per_page() const {
        double usable = page_height - margin_top - margin_bottom;
        if (line_height <= 0.0) return 1;
        int n = static_cast<int>(usable / line_height);
        return n > 0 ? n : 1;
    }
};

// ============================================================================
// PDF Report Writer
// ============================================================================

class PdfReportWriter {
public:
    explicit PdfReportWriter(const PdfConfig & config = PdfConfig());
    ~PdfReportWriter() = default;

    // ---- Content input ----

    // Append a single line of text. Pagination is automatic: when the current
    // page is full a new page is started transparently.
    void add_line(const std::string & line);

    // Append a block of text, splitting on newlines.
    void add_text(const std::string & text);

    // Force a page break.
    void new_page();

    // ---- Output ----

    // Render the accumulated content to a complete PDF byte stream.
    std::string render() const;

    // Render and write to a file. Returns true on success.
    bool save(const std::string & path) const;

    // ---- Introspection (for testing) ----
    int  page_count() const;
    int  line_count() const;             // lines on the current (last) page
    int  total_line_count() const;       // lines across all pages
    bool is_empty() const;

    void clear();

private:
    PdfConfig config_;

    // Pages of laid-out lines. There is always at least one (possibly empty)
    // page once content has been added.
    std::vector<std::vector<std::string>> pages_;

    void ensure_page();

    // PDF generation helpers.
    static std::string escape_pdf_string(const std::string & str);
    std::string build_content_stream(const std::vector<std::string> & lines) const;
    static std::string format_number(double value);
};

} // namespace report
} // namespace ggnucash
