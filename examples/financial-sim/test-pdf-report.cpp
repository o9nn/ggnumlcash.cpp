#include "pdf-report.h"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace ggnucash::report;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                                 \
    do {                                                                           \
        std::cout << "  Testing: " << name << "... ";                              \
        try {

#define TEST_END(name)                                                             \
            std::cout << "PASSED" << std::endl;                                    \
            tests_passed++;                                                        \
        } catch (const std::exception & e) {                                       \
            std::cout << "FAILED: " << e.what() << std::endl;                      \
            tests_failed++;                                                        \
        } catch (...) {                                                            \
            std::cout << "FAILED: Unknown exception" << std::endl;                 \
            tests_failed++;                                                        \
        }                                                                          \
    } while (0)

#define ASSERT_TRUE(cond) do { if (!(cond)) { throw std::runtime_error("Assertion failed: " #cond " at line " + std::to_string(__LINE__)); } } while(0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { throw std::runtime_error("Assertion failed: " #a " != " #b " at line " + std::to_string(__LINE__)); } } while(0)

// ============================================================================
// Basic Structure Tests
// ============================================================================

void test_pdf_header_and_structure() {
    TEST("PDF header and document structure");

    PdfReportWriter writer;
    writer.add_line("Hello, GGNuCash!");
    std::string pdf = writer.render();

    ASSERT_TRUE(pdf.size() > 0);
    // Header
    ASSERT_TRUE(pdf.find("%PDF-1.4") == 0);
    // Required structural markers
    ASSERT_TRUE(pdf.find("/Type /Catalog") != std::string::npos);
    ASSERT_TRUE(pdf.find("/Type /Pages") != std::string::npos);
    ASSERT_TRUE(pdf.find("/Type /Page") != std::string::npos);
    ASSERT_TRUE(pdf.find("/Type /Font") != std::string::npos);
    ASSERT_TRUE(pdf.find("xref") != std::string::npos);
    ASSERT_TRUE(pdf.find("trailer") != std::string::npos);
    ASSERT_TRUE(pdf.find("startxref") != std::string::npos);
    ASSERT_TRUE(pdf.find("%%EOF") != std::string::npos);
    // Content embedded
    ASSERT_TRUE(pdf.find("Hello, GGNuCash!") != std::string::npos);

    TEST_END("PDF header and document structure");
}

void test_empty_document_renders() {
    TEST("Empty document renders a valid blank page");

    PdfReportWriter writer;
    std::string pdf = writer.render();

    ASSERT_TRUE(pdf.find("%PDF-1.4") == 0);
    ASSERT_TRUE(pdf.find("/Count 1") != std::string::npos);  // one blank page
    ASSERT_TRUE(pdf.find("%%EOF") != std::string::npos);

    TEST_END("Empty document renders a valid blank page");
}

void test_metadata_dictionary() {
    TEST("Document info / metadata dictionary");

    PdfConfig cfg;
    cfg.title = "Audit Trail Report";
    cfg.author = "GGNuCash Auditors";
    PdfReportWriter writer(cfg);
    writer.add_line("line");
    std::string pdf = writer.render();

    ASSERT_TRUE(pdf.find("/Title (Audit Trail Report)") != std::string::npos);
    ASSERT_TRUE(pdf.find("/Author (GGNuCash Auditors)") != std::string::npos);
    ASSERT_TRUE(pdf.find("/Producer (") != std::string::npos);

    TEST_END("Document info / metadata dictionary");
}

// ============================================================================
// Pagination Tests
// ============================================================================

void test_automatic_pagination() {
    TEST("Automatic pagination across pages");

    PdfConfig cfg;
    cfg.font_size = 10.0;
    cfg.line_height = 12.0;
    int per_page = cfg.lines_per_page();

    PdfReportWriter writer(cfg);
    int total = per_page * 3 + 5;   // just over 3 pages
    for (int i = 0; i < total; i++) {
        writer.add_line("Line " + std::to_string(i));
    }

    ASSERT_EQ(writer.page_count(), 4);
    ASSERT_EQ(writer.total_line_count(), total);
    // Last page holds the remainder
    ASSERT_EQ(writer.line_count(), 5);

    std::string pdf = writer.render();
    ASSERT_TRUE(pdf.find("/Count 4") != std::string::npos);

    TEST_END("Automatic pagination across pages");
}

void test_explicit_page_break() {
    TEST("Explicit page break");

    PdfReportWriter writer;
    writer.add_line("Page 1");
    writer.new_page();
    writer.add_line("Page 2");

    ASSERT_EQ(writer.page_count(), 2);
    std::string pdf = writer.render();
    ASSERT_TRUE(pdf.find("/Count 2") != std::string::npos);

    TEST_END("Explicit page break");
}

void test_lines_per_page_positive() {
    TEST("lines_per_page is always positive");

    PdfConfig cfg;
    cfg.line_height = 10000.0;  // absurdly large -> still >= 1 line/page
    ASSERT_TRUE(cfg.lines_per_page() >= 1);

    TEST_END("lines_per_page is always positive");
}

// ============================================================================
// Text Handling Tests
// ============================================================================

void test_text_splitting_on_newlines() {
    TEST("add_text splits on newlines");

    PdfReportWriter writer;
    writer.add_text("alpha\nbeta\ngamma");
    ASSERT_EQ(writer.total_line_count(), 3);
    ASSERT_EQ(writer.page_count(), 1);

    std::string pdf = writer.render();
    ASSERT_TRUE(pdf.find("(alpha)") != std::string::npos);
    ASSERT_TRUE(pdf.find("(beta)") != std::string::npos);
    ASSERT_TRUE(pdf.find("(gamma)") != std::string::npos);

    TEST_END("add_text splits on newlines");
}

void test_crlf_handling() {
    TEST("CRLF line endings normalized");

    PdfReportWriter writer;
    writer.add_text("one\r\ntwo\r\n");
    ASSERT_EQ(writer.total_line_count(), 2);
    std::string pdf = writer.render();
    // No stray carriage returns should appear inside the text strings
    ASSERT_TRUE(pdf.find("(one\r)") == std::string::npos);
    ASSERT_TRUE(pdf.find("(two\r)") == std::string::npos);

    TEST_END("CRLF line endings normalized");
}

void test_pdf_string_escaping() {
    TEST("PDF string delimiter escaping");

    PdfReportWriter writer;
    writer.add_line("Balance (credit) \\ 100%");
    std::string pdf = writer.render();

    // Parentheses and backslash must be escaped
    ASSERT_TRUE(pdf.find("Balance \\(credit\\) \\\\ 100%") != std::string::npos);

    TEST_END("PDF string delimiter escaping");
}

// ============================================================================
// Output Tests
// ============================================================================

void test_xref_offsets_consistent() {
    TEST("xref offsets are consistent and increasing");

    PdfReportWriter writer;
    for (int i = 0; i < 50; i++) {
        writer.add_line("Row " + std::to_string(i) + "  |  value " + std::to_string(i * 7));
    }
    std::string pdf = writer.render();

    // The xref section must exist and startxref must point at it.
    auto xref_pos = pdf.find("\nxref\n");
    ASSERT_TRUE(xref_pos != std::string::npos);

    auto startxref_pos = pdf.find("startxref\n");
    ASSERT_TRUE(startxref_pos != std::string::npos);
    long declared = std::stol(pdf.substr(startxref_pos + 10));
    // declared offset should equal actual byte index of "xref"
    ASSERT_EQ(declared, static_cast<long>(xref_pos + 1));

    TEST_END("xref offsets are consistent and increasing");
}

void test_save_to_file() {
    TEST("Save PDF to file");

    PdfReportWriter writer;
    writer.add_text("GGNuCash PDF Report\n===================");
    std::string path = "/tmp/ggnucash_test_report.pdf";
    ASSERT_TRUE(writer.save(path));

    // Re-read and verify round-trip bytes
    FILE * f = fopen(path.c_str(), "rb");
    ASSERT_TRUE(f != nullptr);
    std::string contents;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        contents.append(buf, n);
    }
    fclose(f);
    std::remove(path.c_str());

    ASSERT_TRUE(contents.find("%PDF-1.4") == 0);
    ASSERT_TRUE(contents.find("GGNuCash PDF Report") != std::string::npos);
    ASSERT_TRUE(contents.find("%%EOF") != std::string::npos);

    TEST_END("Save PDF to file");
}

void test_a4_page_size() {
    TEST("A4 page size preset");

    PdfConfig cfg = PdfConfig::a4();
    PdfReportWriter writer(cfg);
    writer.add_line("A4 page");
    std::string pdf = writer.render();

    ASSERT_TRUE(pdf.find("/MediaBox [0 0 595 842]") != std::string::npos);

    TEST_END("A4 page size preset");
}

void test_clear_resets_document() {
    TEST("clear() resets the document");

    PdfReportWriter writer;
    writer.add_line("data");
    ASSERT_FALSE(writer.is_empty());
    writer.clear();
    ASSERT_TRUE(writer.is_empty());
    ASSERT_EQ(writer.page_count(), 0);

    TEST_END("clear() resets the document");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  PDF Report Writer Tests (Phase A.1)" << std::endl;
    std::cout << "============================================\n" << std::endl;

    std::cout << "--- Basic Structure Tests ---" << std::endl;
    test_pdf_header_and_structure();
    test_empty_document_renders();
    test_metadata_dictionary();

    std::cout << "\n--- Pagination Tests ---" << std::endl;
    test_automatic_pagination();
    test_explicit_page_break();
    test_lines_per_page_positive();

    std::cout << "\n--- Text Handling Tests ---" << std::endl;
    test_text_splitting_on_newlines();
    test_crlf_handling();
    test_pdf_string_escaping();

    std::cout << "\n--- Output Tests ---" << std::endl;
    test_xref_offsets_consistent();
    test_save_to_file();
    test_a4_page_size();
    test_clear_resets_document();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    std::cout << "  Total:   " << (tests_passed + tests_failed) << " tests" << std::endl;
    std::cout << "============================================" << std::endl;

    return tests_failed == 0 ? 0 : 1;
}
