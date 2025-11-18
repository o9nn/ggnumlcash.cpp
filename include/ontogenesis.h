#pragma once

#include "ggml.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

// Ontogenesis: Self-Generating Kernel System
// Based on B-series expansions and differential operators for
// recursive self-composition, optimization, and evolution of kernels

namespace ontogenesis {

// Development stages of a kernel
enum class DevelopmentStage {
    EMBRYONIC,  // Just generated, basic structure
    JUVENILE,   // Developing, optimizing
    MATURE,     // Fully developed, capable of reproduction
    SENESCENT   // Declining, ready for replacement
};

// Gene types in kernel genome
enum class GeneType {
    COEFFICIENT,   // B-series coefficients (mutable)
    OPERATOR,      // Differential operators (mutable)
    SYMMETRY,      // Domain symmetries (immutable)
    PRESERVATION   // Conserved quantities (immutable)
};

// A single gene in the kernel genome
struct KernelGene {
    GeneType type;
    std::vector<float> values;  // Gene values (coefficients, parameters)
    bool is_mutable;            // Can this gene be modified?
    
    KernelGene(GeneType t, const std::vector<float>& v, bool mutable_gene = true)
        : type(t), values(v), is_mutable(mutable_gene) {}
};

// Genetic information of a kernel - the "DNA"
struct KernelGenome {
    std::string id;                      // Unique identifier
    int generation;                      // Generation number
    std::vector<std::string> lineage;    // Parent IDs
    std::vector<KernelGene> genes;       // Genetic information
    float fitness;                       // Overall fitness score
    int age;                             // Age in generations
    
    KernelGenome() : id(""), generation(0), fitness(0.0f), age(0) {}
    
    // Generate unique ID
    static std::string generate_id();
};

// Development state tracking
struct OntogeneticState {
    DevelopmentStage stage;
    float maturity;              // 0.0 to 1.0
    int development_cycles;      // Number of optimization cycles
    
    OntogeneticState() 
        : stage(DevelopmentStage::EMBRYONIC), maturity(0.0f), development_cycles(0) {}
};

// Development event history
struct DevelopmentEvent {
    int iteration;
    float grip;
    DevelopmentStage stage;
    std::string description;
};

// Core kernel structure with genetic capabilities
struct OntogeneticKernel {
    KernelGenome genome;
    OntogeneticState state;
    std::vector<DevelopmentEvent> history;
    
    // B-series coefficients (the actual computational kernel)
    std::vector<float> coefficients;
    int order;  // Order of the method
    
    OntogeneticKernel() : order(0) {}
    
    // Initialize with basic coefficients
    static OntogeneticKernel create(int order, const std::vector<float>& initial_coeffs);
};

// Grip components for fitness evaluation
struct GripMetrics {
    float contact;      // How well kernel touches domain
    float coverage;     // Completeness of span
    float efficiency;   // Computational cost
    float stability;    // Numerical properties
    float novelty;      // Genetic diversity
    float symmetry;     // Symmetry preservation
    
    // Calculate overall grip
    float total() const {
        return contact * 0.4f + stability * 0.2f + efficiency * 0.2f + 
               novelty * 0.1f + symmetry * 0.1f;
    }
};

// Configuration for evolution
struct EvolutionConfig {
    int population_size;
    float mutation_rate;
    float crossover_rate;
    float elitism_rate;
    int max_generations;
    float fitness_threshold;
    float diversity_pressure;
    
    EvolutionConfig() 
        : population_size(20), mutation_rate(0.1f), crossover_rate(0.7f),
          elitism_rate(0.2f), max_generations(100), fitness_threshold(0.9f),
          diversity_pressure(0.1f) {}
};

// Development schedule configuration
struct DevelopmentSchedule {
    int embryonic_duration;
    int juvenile_duration;
    int mature_duration;
    float maturity_threshold;
    
    DevelopmentSchedule()
        : embryonic_duration(2), juvenile_duration(5), 
          mature_duration(10), maturity_threshold(0.8f) {}
};

// Complete ontogenesis configuration
struct OntogenesisConfig {
    EvolutionConfig evolution;
    DevelopmentSchedule development;
    std::vector<OntogeneticKernel> seed_kernels;
    std::function<float(const OntogeneticKernel&)> fitness_function;
    
    OntogenesisConfig() {
        // Default fitness function uses grip metrics
        fitness_function = [](const OntogeneticKernel& k) {
            return k.genome.fitness;
        };
    }
};

// Generation statistics
struct GenerationStats {
    int generation;
    float best_fitness;
    float average_fitness;
    float worst_fitness;
    float diversity;
    int population_size;
};

// === Core Ontogenesis Operations ===

// Initialize an ontogenetic kernel from basic coefficients
OntogeneticKernel initialize_ontogenetic_kernel(const std::vector<float>& coefficients, int order);

// Self-generation: kernel generates offspring through recursive composition
// Applies chain rule: (f∘f)' = f'(f(x)) · f'(x)
OntogeneticKernel self_generate(const OntogeneticKernel& parent);

// Self-optimization: improve kernel through iterative grip enhancement
OntogeneticKernel self_optimize(const OntogeneticKernel& kernel, int iterations = 10);

// Self-reproduction: two kernels combine to create offspring
enum class ReproductionMethod {
    CROSSOVER,  // Single-point genetic crossover
    MUTATION,   // Random coefficient mutation
    CLONING     // Direct copy
};

OntogeneticKernel self_reproduce(
    const OntogeneticKernel& parent1,
    const OntogeneticKernel& parent2,
    ReproductionMethod method = ReproductionMethod::CROSSOVER
);

// === Genetic Operations ===

// Crossover two genomes
KernelGenome crossover(const KernelGenome& genome1, const KernelGenome& genome2);

// Mutate a genome
void mutate(KernelGenome& genome, float mutation_rate);

// Calculate genetic distance between two kernels
float genetic_distance(const OntogeneticKernel& k1, const OntogeneticKernel& k2);

// === Fitness Evaluation ===

// Evaluate grip metrics for a kernel
GripMetrics evaluate_grip(const OntogeneticKernel& kernel);

// Calculate fitness based on grip and other factors
float calculate_fitness(
    const OntogeneticKernel& kernel,
    const std::vector<OntogeneticKernel>& population
);

// Update fitness for entire population
void update_population_fitness(std::vector<OntogeneticKernel>& population);

// === Evolution Functions ===

// Run evolution for multiple generations
std::vector<GenerationStats> run_ontogenesis(const OntogenesisConfig& config);

// Single generation evolution step
void evolve_generation(
    std::vector<OntogeneticKernel>& population,
    const EvolutionConfig& config
);

// Tournament selection for parent selection
std::vector<OntogeneticKernel> tournament_selection(
    const std::vector<OntogeneticKernel>& population,
    int count,
    int tournament_size = 3
);

// Update development stages based on age and fitness
void update_development_stages(
    std::vector<OntogeneticKernel>& population,
    const DevelopmentSchedule& schedule
);

// === Utility Functions ===

// Generate initial random population
std::vector<OntogeneticKernel> generate_initial_population(
    int size,
    int kernel_order,
    const std::vector<OntogeneticKernel>& seeds = {}
);

// Find best kernel in population
const OntogeneticKernel& find_best_kernel(const std::vector<OntogeneticKernel>& population);

// Calculate population diversity
float calculate_diversity(const std::vector<OntogeneticKernel>& population);

// Print kernel information
void print_kernel_info(const OntogeneticKernel& kernel);

// Print generation statistics
void print_generation_stats(const GenerationStats& stats);

} // namespace ontogenesis
