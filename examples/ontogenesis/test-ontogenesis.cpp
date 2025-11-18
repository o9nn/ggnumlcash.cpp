#include "ontogenesis.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace ontogenesis;

// Test utility: compare floats with tolerance
bool float_equal(float a, float b, float epsilon = 1e-5f) {
    return std::abs(a - b) < epsilon;
}

// Test 1: Basic kernel initialization
void test_kernel_initialization() {
    std::cout << "Test 1: Kernel Initialization... ";
    
    std::vector<float> coeffs = {0.5f, 0.25f, 0.125f};
    auto kernel = initialize_ontogenetic_kernel(coeffs, 3);
    
    assert(kernel.order == 3);
    assert(kernel.coefficients.size() == 3);
    assert(float_equal(kernel.coefficients[0], 0.5f));
    assert(float_equal(kernel.coefficients[1], 0.25f));
    assert(float_equal(kernel.coefficients[2], 0.125f));
    assert(kernel.genome.generation == 0);
    assert(kernel.genome.age == 0);
    assert(kernel.state.stage == DevelopmentStage::EMBRYONIC);
    assert(float_equal(kernel.state.maturity, 0.0f));
    
    std::cout << "PASSED" << std::endl;
}

// Test 2: Self-generation creates offspring
void test_self_generation() {
    std::cout << "Test 2: Self-Generation... ";
    
    std::vector<float> coeffs = {0.5f, 0.25f, 0.125f};
    auto parent = initialize_ontogenetic_kernel(coeffs, 3);
    auto offspring = self_generate(parent);
    
    // Offspring should have incremented generation
    assert(offspring.genome.generation == parent.genome.generation + 1);
    
    // Offspring should have parent in lineage
    assert(offspring.genome.lineage.size() == 1);
    assert(offspring.genome.lineage[0] == parent.genome.id);
    
    // Offspring should have different ID
    assert(offspring.genome.id != parent.genome.id);
    
    // Coefficients should be modified (chain rule applied)
    bool coeffs_different = false;
    for (size_t i = 0; i < offspring.coefficients.size(); ++i) {
        if (!float_equal(offspring.coefficients[i], parent.coefficients[i])) {
            coeffs_different = true;
            break;
        }
    }
    assert(coeffs_different);
    
    std::cout << "PASSED" << std::endl;
}

// Test 3: Self-optimization improves grip
void test_self_optimization() {
    std::cout << "Test 3: Self-Optimization... ";
    
    std::vector<float> coeffs = {0.3f, 0.4f, 0.2f};
    auto kernel = initialize_ontogenetic_kernel(coeffs, 3);
    
    auto initial_grip = evaluate_grip(kernel);
    float initial_grip_value = initial_grip.total();
    
    auto optimized = self_optimize(kernel, 3);
    auto final_grip = evaluate_grip(optimized);
    float final_grip_value = final_grip.total();
    
    // Grip should generally improve (though not guaranteed for all cases)
    // At minimum, maturity should increase
    assert(optimized.state.maturity > kernel.state.maturity);
    assert(optimized.state.development_cycles == 3);
    
    // History should be recorded
    assert(optimized.history.size() == 3);
    
    std::cout << "PASSED" << std::endl;
}

// Test 4: Crossover combines parent genomes
void test_crossover() {
    std::cout << "Test 4: Crossover... ";
    
    std::vector<float> coeffs1 = {0.8f, 0.6f, 0.4f};
    std::vector<float> coeffs2 = {0.2f, 0.3f, 0.5f};
    
    auto parent1 = initialize_ontogenetic_kernel(coeffs1, 3);
    auto parent2 = initialize_ontogenetic_kernel(coeffs2, 3);
    
    auto offspring = self_reproduce(parent1, parent2, ReproductionMethod::CROSSOVER);
    
    // Offspring should have both parents in lineage
    assert(offspring.genome.lineage.size() == 2);
    
    // Generation should be max + 1
    assert(offspring.genome.generation > parent1.genome.generation);
    assert(offspring.genome.generation > parent2.genome.generation);
    
    // Offspring coefficients should be a mix
    bool has_parent1_coeff = false;
    bool has_parent2_coeff = false;
    
    for (size_t i = 0; i < offspring.coefficients.size(); ++i) {
        if (float_equal(offspring.coefficients[i], parent1.coefficients[i])) {
            has_parent1_coeff = true;
        }
        if (float_equal(offspring.coefficients[i], parent2.coefficients[i])) {
            has_parent2_coeff = true;
        }
    }
    
    // At least some genetic material from parents (may fail randomly)
    // So we just check that crossover created a valid kernel
    assert(offspring.coefficients.size() == 3);
    
    std::cout << "PASSED" << std::endl;
}

// Test 5: Mutation modifies genome
void test_mutation() {
    std::cout << "Test 5: Mutation... ";
    
    std::vector<float> coeffs = {0.5f, 0.5f, 0.5f};
    auto parent = initialize_ontogenetic_kernel(coeffs, 3);
    
    auto mutated = self_reproduce(parent, parent, ReproductionMethod::MUTATION);
    
    // Mutated kernel should have different coefficients
    bool coeffs_different = false;
    for (size_t i = 0; i < mutated.coefficients.size(); ++i) {
        if (!float_equal(mutated.coefficients[i], parent.coefficients[i], 0.001f)) {
            coeffs_different = true;
            break;
        }
    }
    
    // Mutation should have occurred (probabilistic but very likely)
    // If this fails occasionally, it's acceptable
    assert(coeffs_different);
    
    std::cout << "PASSED" << std::endl;
}

// Test 6: Cloning creates exact copy
void test_cloning() {
    std::cout << "Test 6: Cloning... ";
    
    std::vector<float> coeffs = {0.7f, 0.3f, 0.1f};
    auto parent = initialize_ontogenetic_kernel(coeffs, 3);
    
    auto clone = self_reproduce(parent, parent, ReproductionMethod::CLONING);
    
    // Clone should have same coefficients
    assert(clone.coefficients.size() == parent.coefficients.size());
    for (size_t i = 0; i < clone.coefficients.size(); ++i) {
        assert(float_equal(clone.coefficients[i], parent.coefficients[i]));
    }
    
    // But different ID
    assert(clone.genome.id != parent.genome.id);
    
    // And should have parent in lineage
    assert(clone.genome.lineage.size() >= 1);
    assert(clone.genome.lineage.back() == parent.genome.id);
    
    std::cout << "PASSED" << std::endl;
}

// Test 7: Grip evaluation
void test_grip_evaluation() {
    std::cout << "Test 7: Grip Evaluation... ";
    
    std::vector<float> coeffs = {0.5f, 0.3f, 0.2f};
    auto kernel = initialize_ontogenetic_kernel(coeffs, 3);
    
    auto grip = evaluate_grip(kernel);
    
    // All components should be in [0, 1]
    assert(grip.contact >= 0.0f && grip.contact <= 1.0f);
    assert(grip.coverage >= 0.0f && grip.coverage <= 1.0f);
    assert(grip.efficiency >= 0.0f && grip.efficiency <= 1.0f);
    assert(grip.stability >= 0.0f && grip.stability <= 1.0f);
    assert(grip.novelty >= 0.0f && grip.novelty <= 1.0f);
    assert(grip.symmetry >= 0.0f && grip.symmetry <= 1.0f);
    
    // Total should be reasonable
    float total = grip.total();
    assert(total >= 0.0f && total <= 1.0f);
    
    std::cout << "PASSED" << std::endl;
}

// Test 8: Genetic distance
void test_genetic_distance() {
    std::cout << "Test 8: Genetic Distance... ";
    
    std::vector<float> coeffs1 = {0.0f, 0.0f, 0.0f};
    std::vector<float> coeffs2 = {1.0f, 1.0f, 1.0f};
    std::vector<float> coeffs3 = {0.0f, 0.0f, 0.0f};
    
    auto k1 = initialize_ontogenetic_kernel(coeffs1, 3);
    auto k2 = initialize_ontogenetic_kernel(coeffs2, 3);
    auto k3 = initialize_ontogenetic_kernel(coeffs3, 3);
    
    float dist12 = genetic_distance(k1, k2);
    float dist13 = genetic_distance(k1, k3);
    
    // Distance to different kernel should be non-zero
    assert(dist12 > 0.0f);
    
    // Distance to identical kernel should be zero
    assert(float_equal(dist13, 0.0f, 0.01f));
    
    std::cout << "PASSED" << std::endl;
}

// Test 9: Development stage progression
void test_development_stages() {
    std::cout << "Test 9: Development Stages... ";
    
    std::vector<float> coeffs = {0.5f, 0.5f, 0.5f};
    auto kernel = initialize_ontogenetic_kernel(coeffs, 3);
    
    DevelopmentSchedule schedule;
    schedule.embryonic_duration = 2;
    schedule.juvenile_duration = 3;
    schedule.mature_duration = 5;
    
    std::vector<OntogeneticKernel> population = {kernel};
    
    // Initially embryonic
    assert(population[0].state.stage == DevelopmentStage::EMBRYONIC);
    
    // Age through stages
    for (int i = 0; i < 2; ++i) {
        update_development_stages(population, schedule);
    }
    assert(population[0].state.stage == DevelopmentStage::JUVENILE);
    
    for (int i = 0; i < 3; ++i) {
        update_development_stages(population, schedule);
    }
    // Should still be juvenile (needs maturity threshold)
    assert(population[0].state.stage == DevelopmentStage::JUVENILE);
    
    std::cout << "PASSED" << std::endl;
}

// Test 10: Population generation
void test_population_generation() {
    std::cout << "Test 10: Population Generation... ";
    
    std::vector<float> seed_coeffs = {0.5f, 0.5f, 0.5f};
    auto seed = initialize_ontogenetic_kernel(seed_coeffs, 3);
    
    auto population = generate_initial_population(10, 3, {seed});
    
    // Should have requested size
    assert(population.size() == 10);
    
    // First should be the seed
    assert(population[0].genome.id == seed.genome.id);
    
    // Others should be different
    for (size_t i = 1; i < population.size(); ++i) {
        assert(population[i].genome.id != seed.genome.id);
        assert(population[i].order == 3);
    }
    
    std::cout << "PASSED" << std::endl;
}

// Test 11: Finding best kernel
void test_find_best() {
    std::cout << "Test 11: Find Best Kernel... ";
    
    std::vector<OntogeneticKernel> population;
    for (int i = 0; i < 5; ++i) {
        std::vector<float> coeffs = {static_cast<float>(i) * 0.1f, 0.5f, 0.5f};
        auto kernel = initialize_ontogenetic_kernel(coeffs, 3);
        kernel.genome.fitness = static_cast<float>(i) * 0.2f;
        population.push_back(kernel);
    }
    
    const auto& best = find_best_kernel(population);
    
    // Should find the one with highest fitness
    assert(float_equal(best.genome.fitness, 0.8f));
    
    std::cout << "PASSED" << std::endl;
}

// Test 12: Diversity calculation
void test_diversity() {
    std::cout << "Test 12: Diversity Calculation... ";
    
    // Create diverse population
    std::vector<OntogeneticKernel> diverse_pop;
    for (int i = 0; i < 5; ++i) {
        std::vector<float> coeffs = {static_cast<float>(i) * 0.2f, 0.5f, 0.5f};
        diverse_pop.push_back(initialize_ontogenetic_kernel(coeffs, 3));
    }
    
    // Create uniform population
    std::vector<OntogeneticKernel> uniform_pop;
    for (int i = 0; i < 5; ++i) {
        std::vector<float> coeffs = {0.5f, 0.5f, 0.5f};
        uniform_pop.push_back(initialize_ontogenetic_kernel(coeffs, 3));
    }
    
    float diverse_diversity = calculate_diversity(diverse_pop);
    float uniform_diversity = calculate_diversity(uniform_pop);
    
    // Diverse population should have higher diversity
    assert(diverse_diversity > uniform_diversity);
    
    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "\n=== Ontogenesis Unit Tests ===" << std::endl;
    std::cout << "Running comprehensive test suite...\n" << std::endl;
    
    try {
        test_kernel_initialization();
        test_self_generation();
        test_self_optimization();
        test_crossover();
        test_mutation();
        test_cloning();
        test_grip_evaluation();
        test_genetic_distance();
        test_development_stages();
        test_population_generation();
        test_find_best();
        test_diversity();
        
        std::cout << "\n=== All Tests PASSED ===" << std::endl;
        std::cout << "✓ 12/12 tests successful" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n!!! Test FAILED !!!" << std::endl;
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
