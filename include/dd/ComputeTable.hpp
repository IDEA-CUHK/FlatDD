#pragma once

#include "dd/DDDefinitions.hpp"
#include "dd/Node.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <utility>

namespace dd {

/// Data structure for caching computed results
/// \tparam LeftOperandType type of the operation's left operand
/// \tparam RightOperandType type of the operation's right operand
/// \tparam ResultType type of the operation's result
/// \tparam NBUCKET number of hash buckets to use (has to be a power of two)
template <class LeftOperandType, class RightOperandType, class ResultType,
          std::size_t NBUCKET = 16384>
class ComputeTable {
public:
  ComputeTable() = default;

  struct Entry {
    LeftOperandType leftOperand;
    RightOperandType rightOperand;
    ResultType result;
  };

  static constexpr std::size_t MASK = NBUCKET - 1;

  static std::size_t hash(const LeftOperandType& leftOperand,
                          const RightOperandType& rightOperand) {
    const auto h1 = std::hash<LeftOperandType>{}(leftOperand);
    const auto h2 = std::hash<RightOperandType>{}(rightOperand);
    const auto hash = dd::combineHash(h1, h2);
    return hash & MASK;
  }

  // access functions
  [[nodiscard]] const auto& getTable() const { return table; }

  void insert(const LeftOperandType& leftOperand,
              const RightOperandType& rightOperand, const ResultType& result) {
    const auto key = hash(leftOperand, rightOperand);
    table[key] = {leftOperand, rightOperand, result};
    ++count;
  }

  ResultType* lookup(const LeftOperandType& leftOperand,
                     const RightOperandType& rightOperand,
                     [[maybe_unused]] const bool useDensityMatrix = false) {
    ResultType* result = nullptr;
    lookups++;
    const auto key = hash(leftOperand, rightOperand);
    auto& entry = table[key];
    if (entry.leftOperand != leftOperand) {
      return result;
    }
    if (entry.rightOperand != rightOperand) {
      return result;
    }

    if constexpr (std::is_same_v<RightOperandType, dEdge>) {
      // Since density matrices are reduced representations of matrices, a
      // density matrix may not be returned when a matrix is required and vice
      // versa
      if (!dNode::isTerminal(entry.result.p) &&
          dNode::isDensityMatrixNode(entry.result.p->flags) !=
              useDensityMatrix) {
        return result;
      }
    }
    hits++;
    return &entry.result;
  }

  void clear() {
    if (count > 0) {
      std::fill(table.begin(), table.end(), Entry{});
      count = 0;
    }
  }

  [[nodiscard]] std::size_t getHits() const { return hits; }
  [[nodiscard]] std::size_t getLookups() const { return lookups; }
  [[nodiscard]] fp hitRatio() const {
    if (lookups == 0U) {
      return 1.;
    }
    return static_cast<fp>(getHits()) / static_cast<fp>(getLookups());
  }

  std::ostream& printStatistics(std::ostream& os = std::cout) {
    os << "hits: " << hits << ", looks: " << lookups
       << ", ratio: " << hitRatio() << std::endl;
    return os;
  }

private:
  std::array<Entry, NBUCKET> table{};
  // compute table lookup statistics
  std::size_t hits = 0;
  std::size_t lookups = 0;
  std::size_t count = 0;
};
} // namespace dd
