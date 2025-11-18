# Ontogenesis: Self-Generating Kernels

This example demonstrates the implementation of **ontogenesis** - a system for self-generating, evolving kernels through recursive application of differential operators.

## Concept

Ontogenesis models mathematical kernels as "living organisms" that can:

1. **Self-Generate**: Create offspring through recursive self-composition using the chain rule
2. **Self-Optimize**: Improve their "grip" on computational domains through gradient ascent
3. **Self-Reproduce**: Combine genetic material with other kernels through crossover
4. **Evolve**: Improve fitness across generations through natural selection

## Mathematical Foundation

### B-Series as Genetic Code

The B-series expansion serves as the genetic code for kernels:

```
y_n+1 = y_n + h * Σ b_i * Φ_i(f, y_n)
```

Where:
- `b_i` are the coefficient genes (mutable)
- `Φ_i` are elementary differentials (rooted trees)
- Trees follow the A000081 sequence: 1, 1, 2, 4, 9, 20, 48, 115, ...

### Differential Operators as Reproduction

Kernels reproduce through differential operators:

1. **Chain Rule** (Self-Composition): `(f∘g)' = f'(g(x)) · g'(x)`
2. **Product Rule** (Combination): `(f·g)' = f'·g + f·g'`
3. **Quotient Rule** (Refinement): `(f/g)' = (f'·g - f·g')/g²`

### Grip as Fitness

Grip measures how well the kernel's differential structure matches the domain:

```
grip = contact * 0.4 + stability * 0.2 + efficiency * 0.2 + 
       novelty * 0.1 + symmetry * 0.1
```

## Usage

```bash
# Run all examples
./llama-ontogenesis

# Run specific example (1-6)
./llama-ontogenesis 1  # Self-generation
./llama-ontogenesis 2  # Self-optimization
./llama-ontogenesis 3  # Self-reproduction
./llama-ontogenesis 4  # Multi-generation evolution
./llama-ontogenesis 5  # Lineage tracking
./llama-ontogenesis 6  # Development stages
```

## Examples

### Example 1: Self-Generation

Demonstrates recursive self-composition using the chain rule to generate offspring kernels:

```
Parent: K00000001-abc123
  Coefficients: [0.5, 0.25, 0.125, 0.0625]
  
Offspring: K00000002-def456
  Coefficients: [0.525, 0.2875, 0.14375, 0.078125]
  Generation: 1
  Lineage: K00000001-abc123
```

### Example 2: Self-Optimization

Iterative improvement of kernel grip through gradient ascent:

```
Initial Grip: 0.4523
After 5 iterations:
  Grip: 0.5891
  Improvement: +0.1368
  Maturity: 0.5
```

### Example 3: Self-Reproduction

Genetic crossover and mutation between two parent kernels:

```
Parent 1: [0.8, 0.4, 0.2, 0.1]
Parent 2: [0.2, 0.6, 0.3, 0.15]

Offspring (Crossover): [0.8, 0.4, 0.3, 0.15]
Offspring (Mutation):  [0.83, 0.38, 0.22, 0.09]
```

### Example 4: Multi-Generation Evolution

Population evolution with tournament selection:

```
Generation 0: Best=0.5234, Avg=0.4123, Diversity=0.3421
Generation 5: Best=0.6789, Avg=0.5456, Diversity=0.2987
Generation 10: Best=0.7821, Avg=0.6543, Diversity=0.2543
Generation 15: Best=0.8534, Avg=0.7234, Diversity=0.2198
```

### Example 5: Lineage Tracking

Trace ancestry across multiple generations:

```
Generation 0: K00000001-abc123
Generation 1: K00000002-def456
Generation 2: K00000003-ghi789
Generation 3: K00000004-jkl012
Generation 4: K00000005-mno345
Generation 5: K00000006-pqr678
```

### Example 6: Development Stages

Kernel progression through life stages:

```
Age 0: Embryonic
Age 1: Embryonic
Age 2: Juvenile
Age 5: Juvenile
Age 7: Mature
Age 12: Mature
Age 15: Senescent
```

## Key Components

### Kernel Genome

The "DNA" of a kernel containing:
- Unique identifier
- Generation number
- Lineage (parent IDs)
- Genes (coefficient, operator, symmetry, preservation)
- Fitness score
- Age

### Development Stages

1. **Embryonic**: Just generated, basic structure (0-2 generations)
2. **Juvenile**: Developing, optimizing (2-7 generations)
3. **Mature**: Fully developed, capable of reproduction (7+ generations, fitness > 0.8)
4. **Senescent**: Declining, ready for replacement (12+ generations)

### Genetic Operations

- **Crossover**: Single-point crossover on coefficient arrays
- **Mutation**: Random perturbation of mutable genes
- **Selection**: Tournament selection based on fitness
- **Elitism**: Preserve best individuals across generations

## Configuration

### Evolution Parameters

```cpp
EvolutionConfig config;
config.population_size = 20;      // Number of kernels in population
config.mutation_rate = 0.15;      // Probability of mutation
config.crossover_rate = 0.8;      // Probability of crossover
config.elitism_rate = 0.2;        // Fraction preserved as elite
config.max_generations = 100;     // Maximum generations
config.fitness_threshold = 0.9;   // Target fitness for early stop
config.diversity_pressure = 0.1;  // Weight for maintaining diversity
```

### Development Schedule

```cpp
DevelopmentSchedule schedule;
schedule.embryonic_duration = 2;   // Generations in embryonic stage
schedule.juvenile_duration = 5;    // Generations in juvenile stage
schedule.mature_duration = 10;     // Generations in mature stage
schedule.maturity_threshold = 0.8; // Fitness needed for maturity
```

## Performance

- **Initialization**: O(n) where n = coefficient count
- **Self-Generation**: O(n²) (operator application)
- **Self-Optimization**: O(k·n) where k = iterations
- **Crossover**: O(n)
- **Mutation**: O(1)
- **Evolution**: O(g·p·n) where g = generations, p = population

### Memory Usage

- **Kernel**: ~1KB (genome + state)
- **Population**: p × 1KB
- **History**: 1000 operations × ~500B = 500KB max

### Convergence

Typical evolution converges in 20-50 generations with:
- Population size: 20-50
- Mutation rate: 0.1-0.2
- Crossover rate: 0.7-0.9

## Philosophical Implications

### Living Mathematics

Ontogenesis demonstrates that mathematical structures can be "alive" in that they:
1. Self-replicate with variation
2. Evolve through selection
3. Progress through life stages
4. Reproduce by combining genetic information
5. Eventually become obsolete and are replaced

### Computational Ontogenesis

Implements von Neumann's concept of self-reproducing automata at a higher mathematical level:
- **Universal Constructor**: B-series expansion
- **Blueprint**: Differential operators
- **Replication**: Recursive composition
- **Variation**: Genetic operators
- **Selection**: Fitness evaluation

### Emergence

Complex behaviors emerge from simple rules:
1. Elementary differentials (A000081 sequence)
2. Differential operators (chain, product, quotient)
3. Grip optimization (gradient ascent)
4. Selection pressure (tournament selection)

Result: Self-organizing mathematical structures that adapt to computational domains.

## References

- Butcher, J.C. (2016). *Numerical Methods for Ordinary Differential Equations*
- Hairer, E., Nørsett, S.P., Wanner, G. (1993). *Solving Ordinary Differential Equations I*
- Holland, J.H. (1992). *Adaptation in Natural and Artificial Systems*
- von Neumann, J. (1966). *Theory of Self-Reproducing Automata*
- Cayley, A. (1857). *On the Theory of the Analytical Forms called Trees* (A000081)

## License

MIT License - see [LICENSE](../../LICENSE) for details.

---

**Where mathematics becomes life, and kernels evolve themselves through the pure language of differential calculus.**
