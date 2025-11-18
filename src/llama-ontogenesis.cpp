#include "ontogenesis.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <iostream>

namespace ontogenesis {

// Random number generator
static std::mt19937& get_rng() {
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

// Generate unique ID for a kernel genome
std::string KernelGenome::generate_id() {
    static int counter = 0;
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    std::stringstream ss;
    ss << "K" << std::setfill('0') << std::setw(8) << counter++ 
       << "-" << std::hex << (millis & 0xFFFFFFFF);
    return ss.str();
}

// Create an ontogenetic kernel with initial coefficients
OntogeneticKernel OntogeneticKernel::create(int order, const std::vector<float>& initial_coeffs) {
    OntogeneticKernel kernel;
    kernel.order = order;
    kernel.coefficients = initial_coeffs;
    
    // Initialize genome
    kernel.genome.id = KernelGenome::generate_id();
    kernel.genome.generation = 0;
    kernel.genome.age = 0;
    kernel.genome.fitness = 0.0f;
    
    // Create coefficient genes from initial coefficients
    KernelGene coeff_gene(GeneType::COEFFICIENT, initial_coeffs, true);
    kernel.genome.genes.push_back(coeff_gene);
    
    // Initialize operator genes with default values
    std::vector<float> operator_vals = {1.0f, 0.5f, 0.25f}; // chain, product, quotient weights
    KernelGene operator_gene(GeneType::OPERATOR, operator_vals, true);
    kernel.genome.genes.push_back(operator_gene);
    
    // Initialize state
    kernel.state.stage = DevelopmentStage::EMBRYONIC;
    kernel.state.maturity = 0.0f;
    kernel.state.development_cycles = 0;
    
    return kernel;
}

// Initialize an ontogenetic kernel from basic coefficients
OntogeneticKernel initialize_ontogenetic_kernel(const std::vector<float>& coefficients, int order) {
    return OntogeneticKernel::create(order, coefficients);
}

// Apply chain rule for self-composition: (f∘f)' = f'(f(x)) · f'(x)
OntogeneticKernel self_generate(const OntogeneticKernel& parent) {
    OntogeneticKernel offspring = parent;
    
    // Update genome
    offspring.genome.id = KernelGenome::generate_id();
    offspring.genome.generation = parent.genome.generation + 1;
    offspring.genome.lineage = parent.genome.lineage;
    offspring.genome.lineage.push_back(parent.genome.id);
    offspring.genome.age = 0;
    
    // Apply chain rule to coefficients: compose with self
    // For B-series: (f∘f)(x) has modified coefficients
    std::vector<float> new_coeffs(parent.coefficients.size());
    for (size_t i = 0; i < parent.coefficients.size(); ++i) {
        // Chain rule application: derivative of composition
        float derivative = (i > 0) ? parent.coefficients[i-1] * parent.coefficients[i] : parent.coefficients[i];
        new_coeffs[i] = parent.coefficients[i] + 0.1f * derivative;
    }
    
    offspring.coefficients = new_coeffs;
    
    // Update genes
    if (!offspring.genome.genes.empty()) {
        offspring.genome.genes[0].values = new_coeffs;
    }
    
    // Reset state to embryonic
    offspring.state.stage = DevelopmentStage::EMBRYONIC;
    offspring.state.maturity = 0.0f;
    offspring.state.development_cycles = 0;
    
    // Record development event
    DevelopmentEvent event;
    event.iteration = 0;
    event.stage = DevelopmentStage::EMBRYONIC;
    event.description = "Self-generated from parent " + parent.genome.id;
    offspring.history.push_back(event);
    
    return offspring;
}

// Optimize kernel grip through gradient ascent
OntogeneticKernel self_optimize(const OntogeneticKernel& kernel, int iterations) {
    OntogeneticKernel optimized = kernel;
    
    for (int iter = 0; iter < iterations; ++iter) {
        // Evaluate current grip
        GripMetrics grip = evaluate_grip(optimized);
        float current_grip = grip.total();
        
        // Compute gradient by finite differences
        std::vector<float> gradient(optimized.coefficients.size(), 0.0f);
        const float epsilon = 0.01f;
        
        for (size_t i = 0; i < optimized.coefficients.size(); ++i) {
            // Forward difference
            std::vector<float> perturbed = optimized.coefficients;
            perturbed[i] += epsilon;
            
            OntogeneticKernel temp = optimized;
            temp.coefficients = perturbed;
            if (!temp.genome.genes.empty()) {
                temp.genome.genes[0].values = perturbed;
            }
            
            GripMetrics perturbed_grip = evaluate_grip(temp);
            gradient[i] = (perturbed_grip.total() - current_grip) / epsilon;
        }
        
        // Gradient ascent step
        const float learning_rate = 0.01f;
        for (size_t i = 0; i < optimized.coefficients.size(); ++i) {
            optimized.coefficients[i] += learning_rate * gradient[i];
        }
        
        // Update genes
        if (!optimized.genome.genes.empty()) {
            optimized.genome.genes[0].values = optimized.coefficients;
        }
        
        // Update maturity
        optimized.state.maturity = std::min(1.0f, optimized.state.maturity + 0.1f);
        optimized.state.development_cycles++;
        
        // Record event
        DevelopmentEvent event;
        event.iteration = iter;
        event.grip = current_grip;
        event.stage = optimized.state.stage;
        event.description = "Optimization iteration";
        optimized.history.push_back(event);
    }
    
    return optimized;
}

// Crossover operation: single-point crossover
KernelGenome crossover(const KernelGenome& genome1, const KernelGenome& genome2) {
    KernelGenome offspring;
    offspring.id = KernelGenome::generate_id();
    offspring.generation = std::max(genome1.generation, genome2.generation) + 1;
    offspring.lineage.push_back(genome1.id);
    offspring.lineage.push_back(genome2.id);
    offspring.age = 0;
    
    // Crossover genes
    size_t min_genes = std::min(genome1.genes.size(), genome2.genes.size());
    
    for (size_t i = 0; i < min_genes; ++i) {
        if (genome1.genes[i].is_mutable && genome2.genes[i].is_mutable) {
            // Single-point crossover
            size_t min_len = std::min(genome1.genes[i].values.size(), genome2.genes[i].values.size());
            if (min_len > 0) {
                std::uniform_int_distribution<size_t> dist(0, min_len - 1);
                size_t crossover_point = dist(get_rng());
                
                KernelGene new_gene = genome1.genes[i];
                for (size_t j = crossover_point; j < min_len && j < genome2.genes[i].values.size(); ++j) {
                    new_gene.values[j] = genome2.genes[i].values[j];
                }
                offspring.genes.push_back(new_gene);
            } else {
                offspring.genes.push_back(genome1.genes[i]);
            }
        } else {
            // Immutable genes: randomly choose from parents
            std::uniform_int_distribution<int> dist(0, 1);
            offspring.genes.push_back(dist(get_rng()) == 0 ? genome1.genes[i] : genome2.genes[i]);
        }
    }
    
    return offspring;
}

// Mutation operation
void mutate(KernelGenome& genome, float mutation_rate) {
    std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);
    std::normal_distribution<float> mutation_dist(0.0f, 0.2f);
    
    for (auto& gene : genome.genes) {
        if (gene.is_mutable) {
            for (auto& value : gene.values) {
                if (prob_dist(get_rng()) < mutation_rate) {
                    value += mutation_dist(get_rng());
                }
            }
        }
    }
}

// Self-reproduction with different methods
OntogeneticKernel self_reproduce(
    const OntogeneticKernel& parent1,
    const OntogeneticKernel& parent2,
    ReproductionMethod method
) {
    OntogeneticKernel offspring;
    
    switch (method) {
        case ReproductionMethod::CROSSOVER: {
            offspring.genome = crossover(parent1.genome, parent2.genome);
            
            // Combine coefficients through crossover
            size_t min_size = std::min(parent1.coefficients.size(), parent2.coefficients.size());
            offspring.coefficients.resize(min_size);
            
            std::uniform_int_distribution<size_t> dist(0, min_size > 0 ? min_size - 1 : 0);
            size_t point = min_size > 0 ? dist(get_rng()) : 0;
            
            for (size_t i = 0; i < min_size; ++i) {
                offspring.coefficients[i] = (i < point) ? parent1.coefficients[i] : parent2.coefficients[i];
            }
            break;
        }
        
        case ReproductionMethod::MUTATION: {
            offspring = parent1;
            offspring.genome.id = KernelGenome::generate_id();
            offspring.genome.generation = parent1.genome.generation + 1;
            offspring.genome.lineage.push_back(parent1.genome.id);
            mutate(offspring.genome, 0.2f);
            
            // Apply mutation to coefficients
            std::normal_distribution<float> mutation_dist(0.0f, 0.2f);
            for (auto& coeff : offspring.coefficients) {
                coeff += mutation_dist(get_rng());
            }
            break;
        }
        
        case ReproductionMethod::CLONING: {
            offspring = parent1;
            offspring.genome.id = KernelGenome::generate_id();
            offspring.genome.lineage.push_back(parent1.genome.id);
            break;
        }
    }
    
    offspring.order = parent1.order;
    offspring.state.stage = DevelopmentStage::EMBRYONIC;
    offspring.state.maturity = 0.0f;
    offspring.state.development_cycles = 0;
    
    return offspring;
}

// Calculate genetic distance between two kernels
float genetic_distance(const OntogeneticKernel& k1, const OntogeneticKernel& k2) {
    float distance = 0.0f;
    size_t count = 0;
    
    size_t min_coeff = std::min(k1.coefficients.size(), k2.coefficients.size());
    for (size_t i = 0; i < min_coeff; ++i) {
        float diff = k1.coefficients[i] - k2.coefficients[i];
        distance += diff * diff;
        count++;
    }
    
    return (count > 0) ? std::sqrt(distance / count) : 0.0f;
}

// Evaluate grip metrics for a kernel
GripMetrics evaluate_grip(const OntogeneticKernel& kernel) {
    GripMetrics metrics;
    
    // Contact: measure coefficient magnitude distribution
    float sum = 0.0f;
    for (float c : kernel.coefficients) {
        sum += std::abs(c);
    }
    metrics.contact = std::min(1.0f, sum / (kernel.coefficients.size() + 1.0f));
    
    // Coverage: how many non-zero coefficients
    int non_zero = 0;
    for (float c : kernel.coefficients) {
        if (std::abs(c) > 1e-6f) non_zero++;
    }
    metrics.coverage = static_cast<float>(non_zero) / std::max(1, static_cast<int>(kernel.coefficients.size()));
    
    // Efficiency: prefer simpler (lower order) methods
    metrics.efficiency = 1.0f / (1.0f + kernel.order * 0.1f);
    
    // Stability: measure coefficient smoothness
    float variation = 0.0f;
    for (size_t i = 1; i < kernel.coefficients.size(); ++i) {
        float diff = kernel.coefficients[i] - kernel.coefficients[i-1];
        variation += diff * diff;
    }
    metrics.stability = 1.0f / (1.0f + variation);
    
    // Novelty and symmetry initialized to default values
    metrics.novelty = 0.5f;
    metrics.symmetry = 0.5f;
    
    return metrics;
}

// Calculate fitness including population diversity
float calculate_fitness(
    const OntogeneticKernel& kernel,
    const std::vector<OntogeneticKernel>& population
) {
    GripMetrics grip = evaluate_grip(kernel);
    float grip_fitness = grip.total();
    
    // Calculate novelty (average distance to population)
    float total_distance = 0.0f;
    int count = 0;
    for (const auto& other : population) {
        if (other.genome.id != kernel.genome.id) {
            total_distance += genetic_distance(kernel, other);
            count++;
        }
    }
    float novelty = (count > 0) ? (total_distance / count) : 0.5f;
    novelty = std::min(1.0f, novelty);
    
    // Combined fitness
    return grip_fitness * 0.9f + novelty * 0.1f;
}

// Update fitness for entire population
void update_population_fitness(std::vector<OntogeneticKernel>& population) {
    for (auto& kernel : population) {
        kernel.genome.fitness = calculate_fitness(kernel, population);
    }
}

// Tournament selection
std::vector<OntogeneticKernel> tournament_selection(
    const std::vector<OntogeneticKernel>& population,
    int count,
    int tournament_size
) {
    std::vector<OntogeneticKernel> selected;
    std::uniform_int_distribution<size_t> dist(0, population.size() - 1);
    
    for (int i = 0; i < count; ++i) {
        const OntogeneticKernel* best = nullptr;
        float best_fitness = -1.0f;
        
        for (int j = 0; j < tournament_size; ++j) {
            size_t idx = dist(get_rng());
            if (population[idx].genome.fitness > best_fitness) {
                best_fitness = population[idx].genome.fitness;
                best = &population[idx];
            }
        }
        
        if (best) {
            selected.push_back(*best);
        }
    }
    
    return selected;
}

// Update development stages based on age and maturity
void update_development_stages(
    std::vector<OntogeneticKernel>& population,
    const DevelopmentSchedule& schedule
) {
    for (auto& kernel : population) {
        kernel.genome.age++;
        
        if (kernel.genome.age < schedule.embryonic_duration) {
            kernel.state.stage = DevelopmentStage::EMBRYONIC;
        } else if (kernel.genome.age < schedule.embryonic_duration + schedule.juvenile_duration) {
            kernel.state.stage = DevelopmentStage::JUVENILE;
        } else if (kernel.state.maturity >= schedule.maturity_threshold) {
            kernel.state.stage = DevelopmentStage::MATURE;
        } else if (kernel.genome.age > schedule.embryonic_duration + schedule.juvenile_duration + schedule.mature_duration) {
            kernel.state.stage = DevelopmentStage::SENESCENT;
        }
    }
}

// Evolve one generation
void evolve_generation(
    std::vector<OntogeneticKernel>& population,
    const EvolutionConfig& config
) {
    // Update fitness
    update_population_fitness(population);
    
    // Sort by fitness
    std::sort(population.begin(), population.end(),
        [](const OntogeneticKernel& a, const OntogeneticKernel& b) {
            return a.genome.fitness > b.genome.fitness;
        });
    
    // Keep elite individuals
    int elite_count = static_cast<int>(population.size() * config.elitism_rate);
    std::vector<OntogeneticKernel> new_population(population.begin(), population.begin() + elite_count);
    
    // Generate offspring
    while (new_population.size() < static_cast<size_t>(config.population_size)) {
        auto parents = tournament_selection(population, 2, 3);
        
        if (parents.size() >= 2) {
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            
            OntogeneticKernel offspring;
            if (dist(get_rng()) < config.crossover_rate) {
                offspring = self_reproduce(parents[0], parents[1], ReproductionMethod::CROSSOVER);
            } else {
                offspring = self_reproduce(parents[0], parents[1], ReproductionMethod::CLONING);
            }
            
            // Apply mutation
            if (dist(get_rng()) < config.mutation_rate) {
                mutate(offspring.genome, config.mutation_rate);
            }
            
            new_population.push_back(offspring);
        }
    }
    
    population = new_population;
}

// Generate initial random population
std::vector<OntogeneticKernel> generate_initial_population(
    int size,
    int kernel_order,
    const std::vector<OntogeneticKernel>& seeds
) {
    std::vector<OntogeneticKernel> population;
    
    // Add seed kernels
    for (const auto& seed : seeds) {
        population.push_back(seed);
    }
    
    // Generate random kernels
    std::uniform_real_distribution<float> coeff_dist(-1.0f, 1.0f);
    
    while (population.size() < static_cast<size_t>(size)) {
        std::vector<float> coeffs(kernel_order);
        for (int i = 0; i < kernel_order; ++i) {
            coeffs[i] = coeff_dist(get_rng());
        }
        
        population.push_back(initialize_ontogenetic_kernel(coeffs, kernel_order));
    }
    
    return population;
}

// Find best kernel in population
const OntogeneticKernel& find_best_kernel(const std::vector<OntogeneticKernel>& population) {
    return *std::max_element(population.begin(), population.end(),
        [](const OntogeneticKernel& a, const OntogeneticKernel& b) {
            return a.genome.fitness < b.genome.fitness;
        });
}

// Calculate population diversity
float calculate_diversity(const std::vector<OntogeneticKernel>& population) {
    if (population.size() < 2) return 0.0f;
    
    float total_distance = 0.0f;
    int count = 0;
    
    for (size_t i = 0; i < population.size(); ++i) {
        for (size_t j = i + 1; j < population.size(); ++j) {
            total_distance += genetic_distance(population[i], population[j]);
            count++;
        }
    }
    
    return (count > 0) ? (total_distance / count) : 0.0f;
}

// Run complete ontogenesis evolution
std::vector<GenerationStats> run_ontogenesis(const OntogenesisConfig& config) {
    std::vector<GenerationStats> stats;
    
    // Generate initial population
    auto population = generate_initial_population(
        config.evolution.population_size,
        4, // default kernel order
        config.seed_kernels
    );
    
    for (int gen = 0; gen < config.evolution.max_generations; ++gen) {
        // Evolve generation
        evolve_generation(population, config.evolution);
        update_development_stages(population, config.development);
        
        // Collect statistics
        GenerationStats gen_stats;
        gen_stats.generation = gen;
        gen_stats.population_size = static_cast<int>(population.size());
        
        const auto& best = find_best_kernel(population);
        gen_stats.best_fitness = best.genome.fitness;
        
        float sum = 0.0f;
        float min_fit = 1.0f;
        for (const auto& k : population) {
            sum += k.genome.fitness;
            min_fit = std::min(min_fit, k.genome.fitness);
        }
        gen_stats.average_fitness = sum / population.size();
        gen_stats.worst_fitness = min_fit;
        gen_stats.diversity = calculate_diversity(population);
        
        stats.push_back(gen_stats);
        
        // Check for convergence
        if (best.genome.fitness >= config.evolution.fitness_threshold) {
            break;
        }
    }
    
    return stats;
}

// Print kernel information
void print_kernel_info(const OntogeneticKernel& kernel) {
    std::cout << "Kernel ID: " << kernel.genome.id << "\n";
    std::cout << "Generation: " << kernel.genome.generation << "\n";
    std::cout << "Age: " << kernel.genome.age << "\n";
    std::cout << "Fitness: " << std::fixed << std::setprecision(4) << kernel.genome.fitness << "\n";
    std::cout << "Stage: ";
    switch (kernel.state.stage) {
        case DevelopmentStage::EMBRYONIC: std::cout << "Embryonic"; break;
        case DevelopmentStage::JUVENILE: std::cout << "Juvenile"; break;
        case DevelopmentStage::MATURE: std::cout << "Mature"; break;
        case DevelopmentStage::SENESCENT: std::cout << "Senescent"; break;
    }
    std::cout << "\n";
    std::cout << "Maturity: " << std::fixed << std::setprecision(2) << kernel.state.maturity << "\n";
    std::cout << "Coefficients: [";
    for (size_t i = 0; i < kernel.coefficients.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << std::fixed << std::setprecision(4) << kernel.coefficients[i];
    }
    std::cout << "]\n";
    
    if (!kernel.genome.lineage.empty()) {
        std::cout << "Lineage: ";
        for (size_t i = 0; i < kernel.genome.lineage.size(); ++i) {
            if (i > 0) std::cout << " <- ";
            std::cout << kernel.genome.lineage[i];
        }
        std::cout << "\n";
    }
}

// Print generation statistics
void print_generation_stats(const GenerationStats& stats) {
    std::cout << "Generation " << stats.generation << ": ";
    std::cout << "Best=" << std::fixed << std::setprecision(4) << stats.best_fitness;
    std::cout << ", Avg=" << std::fixed << std::setprecision(4) << stats.average_fitness;
    std::cout << ", Diversity=" << std::fixed << std::setprecision(4) << stats.diversity;
    std::cout << "\n";
}

} // namespace ontogenesis
