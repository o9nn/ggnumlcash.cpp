#include "pdf-report.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace ggnucash {
namespace report {

// ============================================================================
// Construction
// ============================================================================

PdfReportWriter::PdfReportWriter(const PdfConfig & config) : config_(config) {}

// ============================================================================
// Content input
// ============================================================================

void PdfReportWriter::ensure_page() {
    if (pages_.empty()) {
        pages_.emplace_back();
    }
}

void PdfReportWriter::add_line(const std::string & line) {
    ensure_page();
    auto & current = pages_.back();
    if (static_cast<int>(current.size()) >= config_.lines_per_page()) {
        pages_.emplace_back();
    }
    pages_.back().push_back(line);
}

void PdfReportWriter::add_text(const std::string & text) {
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        // Strip a trailing carriage return to tolerate CRLF input.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        add_line(line);
    }
}

void PdfReportWriter::new_page() {
    ensure_page();
    if (!pages_.back().empty()) {
        pages_.emplace_back();
    }
}

// ============================================================================
// Introspection
// ============================================================================

int PdfReportWriter::page_count() const {
    return static_cast<int>(pages_.size());
}

int PdfReportWriter::line_count() const {
    if (pages_.empty()) return 0;
    return static_cast<int>(pages_.back().size());
}

int PdfReportWriter::total_line_count() const {
    int total = 0;
    for (const auto & p : pages_) {
        total += static_cast<int>(p.size());
    }
    return total;
}

bool PdfReportWriter::is_empty() const {
    return total_line_count() == 0;
}

void PdfReportWriter::clear() {
    pages_.clear();
}

// ============================================================================
// Helpers
// ============================================================================

std::string PdfReportWriter::escape_pdf_string(const std::string & str) {
    std::string out;
    out.reserve(str.size());
    for (char c : str) {
        switch (c) {
            case '(':  out += "\\("; break;
            case ')':  out += "\\)"; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += " ";   break;  // tabs -> space (undefined in PDF)
            default:
                // Drop other control characters that are invalid in PDF text.
                if (static_cast<unsigned char>(c) >= 0x20 || c == 0x00) {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string PdfReportWriter::format_number(double value) {
    // Compact fixed-point formatting, trimming trailing zeros.
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << value;
    std::string s = ss.str();
    if (s.find('.') != std::string::npos) {
        s.erase(s.find_last_not_of('0') + 1, std::string::npos);
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    if (s.empty() || s == "-0") s = "0";
    return s;
}

std::string PdfReportWriter::build_content_stream(
    const std::vector<std::string> & lines) const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    // Select font resource F1 and set size.
    ss << "BT\n";
    ss << "/F1 " << format_number(config_.font_size) << " Tf\n";
    ss << format_number(config_.line_height) << " TL\n";

    // Position at the top-left text origin.
    double start_x = config_.margin_left;
    double start_y = config_.page_height - config_.margin_top - config_.font_size;
    ss << format_number(start_x) << " " << format_number(start_y) << " Td\n";

    bool first = true;
    for (const auto & line : lines) {
        if (!first) {
            // Move to next line (down by leading).
            ss << "T*\n";
        }
        ss << "(" << escape_pdf_string(line) << ") Tj\n";
        first = false;
    }

    ss << "ET\n";
    return ss.str();
}

// ============================================================================
// Rendering
// ============================================================================

std::string PdfReportWriter::render() const {
    // Object layout:
    //   1: Catalog
    //   2: Pages
    //   3: Font
    //   4..: for each page i -> Page object then Content stream object
    //   last: Info (metadata) dictionary
    const int num_pages = pages_.empty() ? 1 : static_cast<int>(pages_.size());
    const int font_obj = 3;
    const int first_page_obj = 4;
    const int info_obj = first_page_obj + (num_pages * 2);

    std::ostringstream out;
    std::vector<long> offsets;  // 1-indexed by object number via offsets[n-1]

    auto begin_object = [&](int num) {
        offsets.push_back(static_cast<long>(out.tellp()));
        out << num << " 0 obj\n";
    };

    out << "%PDF-1.4\n";
    // Binary marker comment (recommended so transports treat file as binary).
    out << "%\xE2\xE3\xCF\xD3\n";

    // Object 1: Catalog
    begin_object(1);
    out << "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

    // Object 2: Pages (kids listed later; build kid references)
    begin_object(2);
    out << "<< /Type /Pages /Count " << num_pages << " /Kids [";
    for (int i = 0; i < num_pages; i++) {
        int page_obj = first_page_obj + (i * 2);
        out << " " << page_obj << " 0 R";
    }
    out << " ] >>\nendobj\n";

    // Object 3: Font (standard Helvetica/Courier base font)
    begin_object(font_obj);
    out << "<< /Type /Font /Subtype /Type1 /BaseFont /" << config_.font_name
        << " /Encoding /WinAnsiEncoding >>\nendobj\n";

    // Page + content objects.
    std::vector<std::string> empty;
    for (int i = 0; i < num_pages; i++) {
        const auto & lines = pages_.empty() ? empty : pages_[i];
        std::string content = build_content_stream(lines);

        int page_obj = first_page_obj + (i * 2);
        int content_obj = page_obj + 1;

        begin_object(page_obj);
        out << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
            << format_number(config_.page_width) << " "
            << format_number(config_.page_height) << "] "
            << "/Resources << /Font << /F1 " << font_obj << " 0 R >> >> "
            << "/Contents " << content_obj << " 0 R >>\nendobj\n";

        begin_object(content_obj);
        out << "<< /Length " << content.size() << " >>\nstream\n"
            << content << "endstream\nendobj\n";
    }

    // Info (metadata) dictionary.
    begin_object(info_obj);
    out << "<< /Title (" << escape_pdf_string(config_.title) << ")"
        << " /Author (" << escape_pdf_string(config_.author) << ")"
        << " /Creator (" << escape_pdf_string(config_.creator) << ")"
        << " /Producer (" << escape_pdf_string(config_.creator) << ")"
        << " >>\nendobj\n";

    // Cross-reference table.
    int total_objects = info_obj;
    long xref_pos = static_cast<long>(out.tellp());
    out << "xref\n0 " << (total_objects + 1) << "\n";
    out << "0000000000 65535 f \n";
    for (int i = 1; i <= total_objects; i++) {
        std::ostringstream entry;
        entry << std::setfill('0') << std::setw(10) << offsets[i - 1]
              << " 00000 n \n";
        out << entry.str();
    }

    // Trailer.
    out << "trailer\n<< /Size " << (total_objects + 1)
        << " /Root 1 0 R /Info " << info_obj << " 0 R >>\n";
    out << "startxref\n" << xref_pos << "\n%%EOF\n";
    return out.str();
}

bool PdfReportWriter::save(const std::string & path) const {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    std::string data = render();
    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

} // namespace report
} // namespace ggnucash
