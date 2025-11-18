#include "ontogenesis.h"
#include <iostream>
#include <iomanip>

using namespace ontogenesis;

// Example 1: Simple Self-Generation
void example_self_generation() {
    std::cout << "\n=== Example 1: Self-Generation ===" << std::endl;
    std::cout << "Demonstrating recursive self-composition using chain rule\n" << std::endl;
    
    // Create initial kernel with simple coefficients
    std::vector<float> initial_coeffs = {0.5f, 0.25f, 0.125f, 0.0625f};
    auto parent = initialize_ontogenetic_kernel(initial_coeffs, 4);
    
    std::cout << "Parent Kernel:" << std::endl;
    print_kernel_info(parent);
    
    // Generate offspring through self-composition
    auto offspring = self_generate(parent);
    
    std::cout << "\nOffspring Kernel (after self-generation):" << std::endl;
    print_kernel_info(offspring);
    
    std::cout << "\nGeneration lineage: " << offspring.genome.lineage[0] 
              << " -> " << offspring.genome.id << std::endl;
}

// Example 2: Self-Optimization
void example_self_optimization() {
    std::cout << "\n=== Example 2: Self-Optimization ===" << std::endl;
    std::cout << "Demonstrating iterative grip improvement\n" << std::endl;
    
    // Create kernel
    std::vector<float> coeffs = {0.3f, 0.4f, 0.2f, 0.1f};
    auto kernel = initialize_ontogenetic_kernel(coeffs, 4);
    
    std::cout << "Initial Kernel:" << std::endl;
    auto initial_grip = evaluate_grip(kernel);
    std::cout << "  Grip: " << std::fixed << std::setprecision(4) << initial_grip.total() << std::endl;
    std::cout << "  Maturity: " << kernel.state.maturity << std::endl;
    
    // Optimize
    auto optimized = self_optimize(kernel, 5);
    
    std::cout << "\nOptimized Kernel (after 5 iterations):" << std::endl;
    auto final_grip = evaluate_grip(optimized);
    std::cout << "  Grip: " << std::fixed << std::setprecision(4) << final_grip.total() << std::endl;
    std::cout << "  Maturity: " << optimized.state.maturity << std::endl;
    std::cout << "  Development cycles: " << optimized.state.development_cycles << std::endl;
    
    std::cout << "\nGrip improvement: " << std::fixed << std::setprecision(4) 
              << (final_grip.total() - initial_grip.total()) << std::endl;
}

// Example 3: Self-Reproduction
void example_self_reproduction() {
    std::cout << "\n=== Example 3: Self-Reproduction ===" << std::endl;
    std::cout << "Demonstrating genetic crossover between two kernels\n" << std::endl;
    
    // Create two parent kernels with different characteristics
    std::vector<float> coeffs1 = {0.8f, 0.4f, 0.2f, 0.1f};
    std::vector<float> coeffs2 = {0.2f, 0.6f, 0.3f, 0.15f};
    
    auto parent1 = initialize_ontogenetic_kernel(coeffs1, 4);
    auto parent2 = initialize_ontogenetic_kernel(coeffs2, 4);
    
    std::cout << "Parent 1 ID: " << parent1.genome.id << std::endl;
    std::cout << "Parent 2 ID: " << parent2.genome.id << std::endl;
    
    // Crossover
    auto offspring_cross = self_reproduce(parent1, parent2, ReproductionMethod::CROSSOVER);
    std::cout << "\nOffspring (Crossover):" << std::endl;
    print_kernel_info(offspring_cross);
    
    // Mutation
    auto offspring_mut = self_reproduce(parent1, parent2, ReproductionMethod::MUTATION);
    std::cout << "\nOffspring (Mutation):" << std::endl;
    print_kernel_info(offspring_mut);
}

// Example 4: Multi-Generation Evolution
void example_evolution() {
    std::cout << "\n=== Example 4: Multi-Generation Evolution ===" << std::endl;
    std::cout << "Evolving a population of kernels over multiple generations\n" << std::endl;
    
    // Create seed kernels
    std::vector<float> seed_coeffs1 = {0.5f, 0.3f, 0.2f, 0.1f};
    std::vector<float> seed_coeffs2 = {0.4f, 0.4f, 0.3f, 0.15f};
    
    auto seed1 = initialize_ontogenetic_kernel(seed_coeffs1, 4);
    auto seed2 = initialize_ontogenetic_kernel(seed_coeffs2, 4);
    
    // Configure evolution
    OntogenesisConfig config;
    config.evolution.population_size = 10;
    config.evolution.mutation_rate = 0.15f;
    config.evolution.crossover_rate = 0.8f;
    config.evolution.elitism_rate = 0.2f;
    config.evolution.max_generations = 20;
    config.evolution.fitness_threshold = 0.85f;
    config.seed_kernels = {seed1, seed2};
    
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Population: " << config.evolution.population_size << std::endl;
    std::cout << "  Mutation rate: " << config.evolution.mutation_rate << std::endl;
    std::cout << "  Crossover rate: " << config.evolution.crossover_rate << std::endl;
    std::cout << "  Max generations: " << config.evolution.max_generations << std::endl;
    std::cout << "\nEvolution progress:" << std::endl;
    
    // Run evolution
    auto generations = run_ontogenesis(config);
    
    // Print results
    std::cout << "\nEvolution Results:" << std::endl;
    for (const auto& gen : generations) {
        print_generation_stats(gen);
    }
    
    std::cout << "\nFinal statistics:" << std::endl;
    const auto& final_gen = generations.back();
    std::cout << "  Generations completed: " << final_gen.generation + 1 << std::endl;
    std::cout << "  Best fitness achieved: " << std::fixed << std::setprecision(4) 
              << final_gen.best_fitness << std::endl;
    std::cout << "  Average fitness: " << std::fixed << std::setprecision(4) 
              << final_gen.average_fitness << std::endl;
    std::cout << "  Population diversity: " << std::fixed << std::setprecision(4) 
              << final_gen.diversity << std::endl;
}

// Example 5: Lineage Tracking
void example_lineage() {
    std::cout << "\n=== Example 5: Lineage Tracking ===" << std::endl;
    std::cout << "Tracking kernel ancestry across generations\n" << std::endl;
    
    // Create ancestor
    std::vector<float> ancestor_coeffs = {0.5f, 0.3f, 0.2f, 0.1f};
    auto current = initialize_ontogenetic_kernel(ancestor_coeffs, 4);
    
    std::cout << "Generation 0 (Ancestor): " << current.genome.id << std::endl;
    
    // Generate 5 generations
    for (int i = 1; i <= 5; ++i) {
        current = self_generate(current);
        std::cout << "Generation " << i << ": " << current.genome.id << std::endl;
        
        auto grip = evaluate_grip(current);
        std::cout << "  Fitness: " << std::fixed << std::setprecision(4) << grip.total() << std::endl;
    }
    
    std::cout << "\nFinal lineage:" << std::endl;
    for (size_t i = 0; i < current.genome.lineage.size(); ++i) {
        std::cout << "  " << i << ": " << current.genome.lineage[i] << std::endl;
    }
}

// Example 6: Development Stages
void example_development_stages() {
    std::cout << "\n=== Example 6: Development Stages ===" << std::endl;
    std::cout << "Tracking kernel progression through life stages\n" << std::endl;
    
    std::vector<float> coeffs = {0.4f, 0.3f, 0.2f, 0.1f};
    auto kernel = initialize_ontogenetic_kernel(coeffs, 4);
    
    DevelopmentSchedule schedule;
    std::vector<OntogeneticKernel> population = {kernel};
    
    std::cout << "Initial stage: ";
    switch (kernel.state.stage) {
        case DevelopmentStage::EMBRYONIC: std::cout << "Embryonic"; break;
        case DevelopmentStage::JUVENILE: std::cout << "Juvenile"; break;
        case DevelopmentStage::MATURE: std::cout << "Mature"; break;
        case DevelopmentStage::SENESCENT: std::cout << "Senescent"; break;
    }
    std::cout << std::endl;
    
    // Age the kernel through stages
    for (int age = 0; age < 15; ++age) {
        update_development_stages(population, schedule);
        
        const char* stage_name = "";
        switch (population[0].state.stage) {
            case DevelopmentStage::EMBRYONIC: stage_name = "Embryonic"; break;
            case DevelopmentStage::JUVENILE: stage_name = "Juvenile"; break;
            case DevelopmentStage::MATURE: stage_name = "Mature"; break;
            case DevelopmentStage::SENESCENT: stage_name = "Senescent"; break;
        }
        
        std::cout << "Age " << population[0].genome.age << ": " << stage_name << std::endl;
    }
}

int main(int argc, char** argv) {
    std::cout << "╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║         ONTOGENESIS: Self-Generating Kernels              ║" << std::endl;
    std::cout << "║     Demonstrating recursive self-composition through      ║" << std::endl;
    std::cout << "║          differential operators and evolution             ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    
    std::cout << "\nOntogenesis enables kernels to:" << std::endl;
    std::cout << "  • Self-generate through recursive composition (chain rule)" << std::endl;
    std::cout << "  • Self-optimize through grip improvement" << std::endl;
    std::cout << "  • Self-reproduce through genetic operators" << std::endl;
    std::cout << "  • Evolve across generations" << std::endl;
    
    // Run examples
    if (argc > 1) {
        int example = std::stoi(argv[1]);
        switch (example) {
            case 1: example_self_generation(); break;
            case 2: example_self_optimization(); break;
            case 3: example_self_reproduction(); break;
            case 4: example_evolution(); break;
            case 5: example_lineage(); break;
            case 6: example_development_stages(); break;
            default:
                std::cout << "Invalid example number. Use 1-6." << std::endl;
                return 1;
        }
    } else {
        // Run all examples
        example_self_generation();
        example_self_optimization();
        example_self_reproduction();
        example_evolution();
        example_lineage();
        example_development_stages();
    }
    
    std::cout << "\n╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║              Ontogenesis Demo Complete                    ║" << std::endl;
    std::cout << "║   Kernels have evolved through differential calculus!     ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    
    return 0;
}
