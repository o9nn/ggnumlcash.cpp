#include "membrane-reconciler.h"

#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

using namespace ggnucash::membrane;

// ============================================================================
// Test Utilities (same shape as test-transaction-validator.cpp)
// ============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct test_register_##name { \
        test_register_##name() { \
            std::cout << "Running: " #name "... "; \
            try { \
                test_##name(); \
                std::cout << "PASSED\n"; \
                tests_passed++; \
            } catch (const std::exception & e) { \
                std::cout << "FAILED: " << e.what() << "\n"; \
                tests_failed++; \
            } catch (...) { \
                std::cout << "FAILED: unknown exception\n"; \
                tests_failed++; \
            } \
        } \
    } test_instance_##name; \
    static void test_##name()

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " == " #b); \
} while(0)

static int64_t count(const multiset & ms, const std::string & token) {
    auto it = ms.find(token);
    return it == ms.end() ? 0 : it->second;
}

static bool has(const multiset & ms, const std::string & token) {
    return count(ms, token) > 0;
}

// The accospace / HOL-Fin-Test fixture: account 62012990132 of AYM, statements
// 202-204, a continuous chain closing on 339.75.
static void add_aym_chain(membrane_reconciler & r) {
    r.add_account("AYM", "62012990132", 33975);
    r.add_statement({"AYM", "62012990132", 202, 10000, 0, 1050, 8950});
    r.add_statement({"AYM", "62012990132", 203, 8950, 25025, 0, 33975});
    r.add_statement({"AYM", "62012990132", 204, 33975, 0, 0, 33975});
}

// ============================================================================
// Statement level: internal reconciliation as annihilation
// ============================================================================

TEST(reconciled_statement_leaves_no_sediment) {
    membrane_reconciler r;
    r.add_account("AYM", "62012990132", 8950);
    r.add_statement({"AYM", "62012990132", 202, 10000, 0, 1050, 8950});
    r.run();
    ASSERT_TRUE(r.sediment().empty());
}

TEST(imbalanced_statement_residual_is_the_imbalance) {
    // fixture statement 900: opens 0, closes 0, carries a -10.50 fee line
    membrane_reconciler r;
    r.add_account("AYM", "62012990132");
    r.add_statement({"AYM", "62012990132", 900, 0, 0, 1050, 0});
    r.run();
    const multiset & sed = r.sediment();
    ASSERT_EQ(count(sed, "d{1}"), 1050);
    ASSERT_EQ(sed.size(), (size_t) 1);
}

TEST(inflow_side_survives_when_inflow_exceeds) {
    membrane_reconciler r;
    r.add_account("RST", "4112241409");
    // opening 10.00 + credits 25.00  vs  closing 25.00 + debits 5.00 : inflow exceeds by 5.00
    r.add_statement({"RST", "4112241409", 7, 1000, 2500, 500, 2500});
    r.run();
    const multiset & sed = r.sediment();
    ASSERT_EQ(count(sed, "o{1}") + count(sed, "c{1}"), 500);
    ASSERT_EQ(count(sed, "z{1}") + count(sed, "d{1}"), 0);
}

// The normal form theorem (isabellex Fin_Membrane.thy): whatever the split of
// o, c, d, z, the residual is |o + c - z - d| tokens of one side.
TEST(normal_form_residual_equals_absolute_imbalance) {
    std::mt19937 rng(20260913);
    std::uniform_int_distribution<int64_t> amt(0, 5000);
    for (int trial = 0; trial < 200; ++trial) {
        int64_t o = amt(rng), c = amt(rng), d = amt(rng), z = amt(rng);
        membrane_reconciler r;
        r.add_account("E", "A");
        r.add_statement({"E", "A", 1, o, c, d, z});
        r.run();
        const multiset & sed = r.sediment();
        int64_t inflow  = count(sed, "o{1}") + count(sed, "c{1}");
        int64_t outflow = count(sed, "z{1}") + count(sed, "d{1}");
        int64_t imbalance = (o + c) - (z + d);
        ASSERT_TRUE(inflow == 0 || outflow == 0);
        ASSERT_EQ(inflow - outflow, imbalance);
    }
}

// ============================================================================
// Account level: statement chain and master balance
// ============================================================================

TEST(continuous_chain_with_master_closing_is_silent) {
    membrane_reconciler r;
    add_aym_chain(r);
    r.run();
    ASSERT_TRUE(r.sediment().empty());
}

TEST(balance_break_residual_is_the_gap) {
    membrane_reconciler r;
    r.add_account("AYM", "62012990132", 33975);
    r.add_statement({"AYM", "62012990132", 202, 10000, 0, 1050, 8950});
    // statement 203 opens 89.52 instead of 89.50: a 2 cent break, still internally reconciled
    r.add_statement({"AYM", "62012990132", 203, 8952, 25025, 0, 33977});
    r.run();
    const multiset & sed = r.sediment();
    ASSERT_EQ(count(sed, "oc{2}"), 2);       // next opening exceeds previous closing by 2 cents
    ASSERT_EQ(count(sed, "zc{2}"), 2);       // last closing exceeds master closing by 2 cents
    ASSERT_EQ(sed.size(), (size_t) 2);
}

TEST(missing_window_leaves_both_chain_copies) {
    // HOL-Fin-Test fixture: PF account 55270018789, statements 5 and 7
    membrane_reconciler r;
    r.add_account("PF", "55270018789", 2500);
    r.add_statement({"PF", "55270018789", 5, 1000, 1000, 0, 2000});
    r.add_statement({"PF", "55270018789", 7, 2500, 500, 500, 2500});
    r.run();
    const multiset & sed = r.sediment();
    ASSERT_EQ(count(sed, "zc{1}"), 2000);    // statement 5 closing, never paired
    ASSERT_EQ(count(sed, "oc{2}"), 2500);    // statement 7 opening, never paired
    ASSERT_EQ(sed.size(), (size_t) 2);
}

TEST(master_closing_gap_survives_as_mz) {
    membrane_reconciler r;
    r.add_account("RST", "4112241409", 9900);   // inventory says 99.00
    r.add_statement({"RST", "4112241409", 7, 1000, 1500, 0, 2500});   // statement closes 25.00
    r.run();
    ASSERT_EQ(count(r.sediment(), "mz{1}"), 7400);
}

// ============================================================================
// Entity and ecosystem level: transfers, inter-company legs, flow patterns
// ============================================================================

TEST(intra_entity_transfer_matches_inside_the_entity) {
    membrane_reconciler r;
    r.add_account("RST", "4112241409", 2500);
    r.add_account("RST", "4112241500", 6000);
    r.add_statement({"RST", "4112241409", 7, 1000, 2000, 500, 2500});
    r.add_statement({"RST", "4112241500", 3, 5500, 500, 0, 6000});
    r.add_payment({4, "4112241409", "4112241500", 500});
    r.build();
    // after wave 3 (step 5) the entity has matched the legs, before it dissolves
    for (int i = 0; i < 5; ++i) r.step();
    for (const membrane & m : r.membranes()) {
        if (m.kind == membrane_kind::ENTITY) {
            ASSERT_TRUE(!has(m.contents, "xo{4}") && !has(m.contents, "xi{4}"));
        }
    }
    r.run();
    ASSERT_TRUE(r.sediment().empty());
}

TEST(inter_company_legs_match_at_the_skin_and_one_sided_leg_survives) {
    membrane_reconciler r;
    r.add_account("AYM", "62012990132", 33975);
    r.add_account("PF",  "55270018789", 2000);
    r.add_statement({"AYM", "62012990132", 204, 33975, 0, 0, 33975});
    r.add_statement({"PF",  "55270018789", 5, 1000, 1000, 0, 2000});
    r.add_payment({2, "62012990132", "55270018789", 1000});                 // both legs seen
    r.add_payment({6, "62012990132", "55270018789", 200, true, false});     // never arrives
    r.run();
    const multiset & sed = r.sediment();
    ASSERT_EQ(count(sed, "icm{2}"), 1);
    ASSERT_EQ(count(sed, "out{6}"), 1);
    ASSERT_TRUE(!has(sed, "in{6}") && !has(sed, "out{2}") && !has(sed, "in{2}"));
}

TEST(round_trip_and_three_cycle_are_detected) {
    membrane_reconciler r;
    r.add_account("AYM", "A1");
    r.add_account("RST", "R1");
    r.add_account("PF",  "P1");
    r.add_payment({1, "R1", "A1", 1500});   // RST -> AYM
    r.add_payment({2, "A1", "P1", 1000});   // AYM -> PF
    r.add_payment({3, "P1", "R1", 500});    // PF  -> RST   closes AYM -> PF -> RST -> AYM
    r.add_payment({5, "A1", "R1", 300});    // AYM -> RST   round trip with payment 1
    r.run();
    const multiset & sed = r.sediment();
    ASSERT_EQ(count(sed, "rt{1,2}"), 1);
    ASSERT_EQ(count(sed, "cyc{1,3,2}"), 1);
    ASSERT_EQ(count(sed, "icm{1}") + count(sed, "icm{2}") + count(sed, "icm{3}") + count(sed, "icm{5}"), 4);
}

// ============================================================================
// Kind synchrony and the wave schedule
// ============================================================================

TEST(kinds_dissolve_in_waves) {
    membrane_reconciler r;
    add_aym_chain(r);
    r.add_account("PF", "55270018789", 2500);
    r.add_statement({"PF", "55270018789", 5, 1000, 1000, 0, 2000});
    r.add_statement({"PF", "55270018789", 7, 2500, 500, 500, 2500});
    r.build();
    ASSERT_EQ(r.live_count(membrane_kind::STATEMENT), 5);
    r.step();                                                    // wave 1: verify
    ASSERT_EQ(r.live_count(membrane_kind::STATEMENT), 5);
    r.step();                                                    // statements dissolve together
    ASSERT_EQ(r.live_count(membrane_kind::STATEMENT), 0);
    ASSERT_EQ(r.live_count(membrane_kind::ACCOUNT), 2);
    r.step();                                                    // wave 2: chain check
    r.step();                                                    // accounts dissolve together
    ASSERT_EQ(r.live_count(membrane_kind::ACCOUNT), 0);
    ASSERT_EQ(r.live_count(membrane_kind::ENTITY), 2);
    r.step();
    r.step();                                                    // entities dissolve together
    ASSERT_EQ(r.live_count(membrane_kind::ENTITY), 0);
    ASSERT_EQ(r.live_count(membrane_kind::ECOSYSTEM), 1);
}

TEST(many_statements_reconcile_in_the_same_number_of_steps) {
    // 2,000 statements across 200 accounts of 20 entities: the wave count does
    // not grow with the size of the supply chain.
    membrane_reconciler r;
    for (int e = 1; e <= 20; ++e) {
        for (int a = 1; a <= 10; ++a) {
            std::string acct = "E" + std::to_string(e) + "A" + std::to_string(a);
            int64_t bal = 10000;
            for (int s = 1; s <= 10; ++s) {
                int64_t cr = 700 * s, db = 300 * s;
                r.add_statement({"E" + std::to_string(e), acct, s, bal, cr, db, bal + cr - db});
                bal = bal + cr - db;
            }
            r.add_account("E" + std::to_string(e), acct, bal);
        }
    }
    int steps = r.run();
    ASSERT_TRUE(r.sediment().empty());
    ASSERT_TRUE(steps <= 8);
}

// ============================================================================
// Bridge to P-Lingua
// ============================================================================

TEST(to_plingua_emits_the_annihilation_system) {
    membrane_reconciler r;
    add_aym_chain(r);
    r.build();
    std::string pli = r.to_plingua();
    ASSERT_TRUE(pli.find("@model<transition>") != std::string::npos);
    ASSERT_TRUE(pli.find("@mu = [ [ [ [ ]'301 [ ]'302 [ ]'303 ]'201 ]'101 ]'0;") != std::string::npos);
    ASSERT_TRUE(pli.find("[o{1}, z{1} --> #]'301;") != std::string::npos);
    ASSERT_TRUE(pli.find("[oc{2}, zc{1} --> #]'201;") != std::string::npos);
    ASSERT_TRUE(pli.find("[mz{1}, zc{3} --> #]'201;") != std::string::npos);
    ASSERT_TRUE(pli.find("[k{1} --> @d]'301;") != std::string::npos);
    ASSERT_TRUE(pli.find("mz{1}*33975") != std::string::npos);
}

TEST(report_explains_every_sediment_token) {
    membrane_reconciler r;
    r.add_account("PF", "55270018789", 2500);
    r.add_statement({"PF", "55270018789", 5, 1000, 1000, 0, 2000});
    r.add_statement({"PF", "55270018789", 7, 2500, 500, 500, 2500});
    r.run();
    std::string rep = r.report();
    ASSERT_TRUE(rep.find("zc{1} * 2000") != std::string::npos);
    ASSERT_TRUE(rep.find("oc{2} * 2500") != std::string::npos);
    ASSERT_TRUE(rep.find("missing window") != std::string::npos);
}

// ============================================================================
// accospace balance-schedule loader (records/atomese/balances/<RECORD>.csv)
// ============================================================================

// Seven rows of the JF_CC_4483810003373008 schedule: a credit-card chain whose
// statement numbers skip (300, 304, 315, 327, 328, 329, 331), so four links are
// MISSING_WINDOW and two are CONSECUTIVE.  Every statement is internally
// reconciled.
static const char * kJfSchedule =
    "record,entity,account_number,currency,statement,statement_number,statement_date,period_start,period_end,"
    "opening_balance,closing_balance,total_credits,total_debits,net_change,transactions,internal_check,"
    "internal_discrepancy,extractor_balance_reconciled,previous_statement,previous_closing,carry_delta,"
    "carry_check,link_class,placeable,source_folder,source_file\n"
    "JF,JF,4483810003373008,ZAR,STMT_300,300,2015-03-30,,,-1549.82,-3615.76,1549.82,-3615.76,-2065.94,4,OK,0.00,true,,,,FIRST,,true,d,f\n"
    "JF,JF,4483810003373008,ZAR,STMT_304,304,2015-07-29,,,-6349.40,-1682.02,6349.40,-1682.02,4667.38,4,OK,0.00,true,STMT_300,-3615.76,-2733.64,BREAK,MISSING_WINDOW,true,d,f\n"
    "JF,JF,4483810003373008,ZAR,STMT_315,315,2016-06-28,,,-1748.00,-1988.79,1748.00,-1988.79,-240.79,4,OK,0.00,true,STMT_304,-1682.02,-65.98,BREAK,MISSING_WINDOW,true,d,f\n"
    "JF,JF,4483810003373008,ZAR,STMT_327,327,2017-06-29,,,-1005.10,-10672.21,1005.10,-10672.21,-9667.11,35,OK,0.00,true,STMT_315,-1988.79,983.69,BREAK,MISSING_WINDOW,true,d,f\n"
    "JF,JF,4483810003373008,ZAR,STMT_328,328,2017-07-28,,,-10672.21,-9285.30,10672.21,-9285.30,1386.91,19,OK,0.00,true,STMT_327,-10672.21,0.00,OK,CONSECUTIVE,true,d,f\n"
    "JF,JF,4483810003373008,ZAR,STMT_329,329,2017-08-29,,,-9285.30,-1500.00,9285.30,-1500.00,7785.30,2,OK,0.00,true,STMT_328,-9285.30,0.00,OK,CONSECUTIVE,true,d,f\n"
    "JF,JF,4483810003373008,ZAR,STMT_331,331,2017-10-30,,,-7492.78,-3380.78,7492.78,-3380.78,4112.00,7,OK,0.00,true,STMT_329,-1500.00,-5992.78,BREAK,MISSING_WINDOW,true,d,f\n";

TEST(balance_schedule_parses_accospace_csv) {
    std::vector<schedule_row> rows = parse_balance_schedule(kJfSchedule);
    ASSERT_EQ(rows.size(), (size_t) 7);
    ASSERT_EQ(rows[0].number, 300);
    ASSERT_EQ(rows[0].opening, -154982);
    ASSERT_EQ(rows[0].closing, -361576);
    ASSERT_EQ(rows[0].credits, 154982);
    ASSERT_EQ(rows[0].debits, 361576);
    ASSERT_TRUE(rows[0].placeable);
    ASSERT_EQ(rows[1].link_class, std::string("MISSING_WINDOW"));
    ASSERT_EQ(rows[4].link_class, std::string("CONSECUTIVE"));
}

// The credit-card schedule holds negative balances.  add_balance_schedule
// translates every balance of the account by one constant so that all tokens are
// non-negative; the residuals are unchanged because both checked identities are
// translation invariant.
TEST(balance_schedule_chain_follows_recorded_link_classes) {
    membrane_reconciler r;
    int64_t shift = 0;
    int added = add_balance_schedule(r, "JF", "4483810003373008", kJfSchedule, -1, &shift);
    ASSERT_EQ(added, 7);
    ASSERT_EQ(shift, 1067221);               // the lowest balance is -10672.21
    r.run();
    const multiset & sed = r.sediment();
    // no statement is internally out of balance
    for (int s = 1; s <= 7; ++s) {
        ASSERT_EQ(count(sed, tok("o", s)) + count(sed, tok("c", s)) + count(sed, tok("z", s)) + count(sed, tok("d", s)), 0);
    }
    // the two CONSECUTIVE links (327->328, 328->329) annihilate completely ...
    ASSERT_EQ(count(sed, "zc{4}"), 0);
    ASSERT_EQ(count(sed, "oc{5}"), 0);
    ASSERT_EQ(count(sed, "zc{5}"), 0);
    ASSERT_EQ(count(sed, "oc{6}"), 0);
    // ... and the four MISSING_WINDOW links leave both (shifted) copies as sediment
    ASSERT_EQ(count(sed, "zc{1}"), -361576 + shift);   // 300 closes -3615.76
    ASSERT_EQ(count(sed, "oc{2}"),  -634940 + shift);  // 304 opens  -6349.40
    ASSERT_EQ(count(sed, "zc{6}"),  -150000 + shift);  // 329 closes -1500.00
    ASSERT_EQ(count(sed, "oc{7}"),  -749278 + shift);  // 331 opens  -7492.78
}

TEST(add_balance_schedule_loads_a_continuous_account) {
    // A hand-made schedule in the accospace column layout: three consecutive
    // statements, one unplaced re-extraction, a master closing that agrees.
    const char * csv =
        "record,entity,account_number,currency,statement,statement_number,statement_date,period_start,period_end,"
        "opening_balance,closing_balance,total_credits,total_debits,net_change,transactions,internal_check,"
        "internal_discrepancy,extractor_balance_reconciled,previous_statement,previous_closing,carry_delta,"
        "carry_check,link_class,placeable,source_folder,source_file\n"
        "A,AYM,62012990132,ZAR,S202,202,2019-01-31,2018-12-31,2019-01-31,100.00,89.50,0.00,-10.50,-10.50,1,OK,0.00,true,,,,FIRST,,true,d,f\n"
        "A,AYM,62012990132,ZAR,S203,203,2019-02-28,2019-01-31,2019-02-28,89.50,339.75,250.25,0.00,250.25,1,OK,0.00,true,S202,89.50,0.00,OK,CONSECUTIVE,true,d,f\n"
        "A,AYM,62012990132,ZAR,S204,204,2019-03-30,2019-02-28,2019-03-30,339.75,339.75,0.00,0.00,0.00,0,OK,0.00,true,S203,339.75,0.00,OK,CONSECUTIVE,true,d,f\n"
        "A,AYM,62012990132,ZAR,S900,900,,,,0.00,0.00,0.00,-10.50,-10.50,1,BREAK,-10.50,false,,,,,,false,d,f\n";
    membrane_reconciler r;
    int added = add_balance_schedule(r, "AYM", "62012990132", csv, 33975);
    ASSERT_EQ(added, 3);                     // the unplaced statement 900 is not loaded
    r.run();
    ASSERT_TRUE(r.sediment().empty());
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n========================================\n";
    std::cout << "Membrane Reconciler Tests\n";
    std::cout << "========================================\n";
    std::cout << "Passed: " << tests_passed << "\n";
    std::cout << "Failed: " << tests_failed << "\n";
    std::cout << "========================================\n";
    return tests_failed == 0 ? 0 : 1;
}
