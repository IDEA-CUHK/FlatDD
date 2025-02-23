#pragma once

#include "dd/Complex.hpp"
#include "dd/ComplexNumbers.hpp"
#include "dd/ComplexValue.hpp"
#include "dd/ComputeTable.hpp"
#include "dd/DDDefinitions.hpp"
#include "dd/DensityNoiseTable.hpp"
#include "dd/Edge.hpp"
#include "dd/GateMatrixDefinitions.hpp"
#include "dd/Package_fwd.hpp"
#include "dd/StochasticNoiseOperationTable.hpp"
#include "dd/UnaryComputeTable.hpp"
#include "dd/UniqueTable.hpp"
#include "operations/Control.hpp"
#include "dd/DMAVTable.hpp"
#include "dd/kernel/kernel.cuh"

#include <cstring> //std::memcpy
#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <immintrin.h>
#include <random>
#include <regex>
#include <set>
#include <stack>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <chrono>
#include <ctime> 
#include <thread>
#include <deque>

#if KANAMESHIKI
#include "extern/MemoryAllocator.KanameShiki/include/KanameShiki.h"
#else 
#include <malloc.h>
#endif

namespace dd {

template <class Config> class SwitchPackage {
  static_assert(std::is_base_of_v<DDPackageConfig, Config>,
                "Config must be derived from DDPackageConfig");

  ///
  /// Construction, destruction, information and reset
  ///
public:
  static constexpr std::size_t MAX_POSSIBLE_QUBITS =
      static_cast<std::size_t>(std::numeric_limits<Qubit>::max()) + 1U;
  static constexpr std::size_t DEFAULT_QUBITS = 32U;
  unsigned int n_thread_exp = 2;
  explicit SwitchPackage(std::size_t nq = DEFAULT_QUBITS) : nqubits(nq) {
    resize(nq);
  };
  ~SwitchPackage() = default;
  SwitchPackage(const SwitchPackage& package) = delete;
  size_t mul_cnt;
  size_t add_cnt;
  SwitchPackage& operator=(const SwitchPackage& package) = delete;

  // resize the SwitchPackage instance
  void resize(std::size_t nq) {
    if (nq > MAX_POSSIBLE_QUBITS) {
      throw std::invalid_argument("Requested too many qubits from SwitchPackage. "
                                  "Qubit datatype only allows up to " +
                                  std::to_string(MAX_POSSIBLE_QUBITS) +
                                  " qubits, while " + std::to_string(nq) +
                                  " were requested. Please recompile the "
                                  "SwitchPackage with a wider Qubit type!");
    }
    nqubits = nq;
    vUniqueTable.resize(nqubits);
    mUniqueTable.resize(nqubits);
    dUniqueTable.resize(nqubits);
    stochasticNoiseOperationCache.resize(nqubits);
    idTable.resize(nqubits);
  }

  // reset SwitchPackage state
  void reset() {
    clearUniqueTables();
    resetMemoryManagers();
    clearComputeTables();
  }

  /// Get the number of qubits
  [[nodiscard]] auto qubits() const { return nqubits; }

private:
  std::size_t nqubits;

  struct DMAVTask {
    Edge<mNode> mat;
    size_t len;

    size_t y_beg;
    size_t z_beg;
  };

public:
  /// The memory manager for vector nodes
  MemoryManager<vNode> vMemoryManager{Config::UT_VEC_INITIAL_ALLOCATION_SIZE};
  /// The memory manager for matrix nodes
  MemoryManager<mNode> mMemoryManager{Config::UT_MAT_INITIAL_ALLOCATION_SIZE};
  /// The memory manager for density matrix nodes
  MemoryManager<dNode> dMemoryManager{Config::UT_DM_INITIAL_ALLOCATION_SIZE};
  /**
   * @brief The memory manager for complex numbers
   * @note The real and imaginary part of complex numbers are treated
   * separately. Hence, it suffices for the manager to only manage real numbers.
   */
  MemoryManager<RealNumber> cMemoryManager{};
  /**
   * @brief The cache manager for complex numbers
   * @note Similar to the memory manager, the cache only maintains real entries,
   * but typically gives them out in pairs to form complex numbers.
   */
  MemoryManager<RealNumber> cCacheManager{};

  /**
   * @brief Get the memory manager for a given type
   * @tparam T The type to get the manager for
   * @return A reference to the manager
   */
  template <class T> [[nodiscard]] auto& getMemoryManager() {
    if constexpr (std::is_same_v<T, vNode>) {
      return vMemoryManager;
    } else if constexpr (std::is_same_v<T, mNode>) {
      return mMemoryManager;
    } else if constexpr (std::is_same_v<T, dNode>) {
      return dMemoryManager;
    } else if constexpr (std::is_same_v<T, RealNumber>) {
      return cMemoryManager;
    }
  }

  /**
   * @brief Reset all memory managers
   * @arg resizeToTotal If set to true, each manager allocates one chunk of
   * memory as large as all chunks combined before the reset.
   * @see MemoryManager::reset
   */
  void resetMemoryManagers(const bool resizeToTotal = false) {
    vMemoryManager.reset(resizeToTotal);
    mMemoryManager.reset(resizeToTotal);
    dMemoryManager.reset(resizeToTotal);
    cMemoryManager.reset(resizeToTotal);
    cCacheManager.reset(resizeToTotal);
  }

  /// The unique table used for vector nodes
  UniqueTable<vNode, Config::UT_VEC_NBUCKET> vUniqueTable{0U, vMemoryManager};
  /// The unique table used for matrix nodes
  UniqueTable<mNode, Config::UT_MAT_NBUCKET> mUniqueTable{0U, mMemoryManager};
  /// The unique table used for density matrix nodes
  UniqueTable<dNode, Config::UT_DM_NBUCKET> dUniqueTable{0U, dMemoryManager};
  /**
   * @brief The unique table used for complex numbers
   * @note The table actually only stores real numbers in the interval [0, 1],
   * but is used to manages all complex numbers throughout the package.
   * @see RealNumberUniqueTable
   */
  RealNumberUniqueTable cUniqueTable{cMemoryManager};
  ComplexNumbers cn{cUniqueTable, cCacheManager};

  /**
   * @brief Get the unique table for a given type
   * @tparam T The type to get the unique table for
   * @return A reference to the unique table
   */
  template <class T> [[nodiscard]] auto& getUniqueTable() {
    if constexpr (std::is_same_v<T, vNode>) {
      return vUniqueTable;
    } else if constexpr (std::is_same_v<T, mNode>) {
      return mUniqueTable;
    } else if constexpr (std::is_same_v<T, dNode>) {
      return dUniqueTable;
    } else if constexpr (std::is_same_v<T, RealNumber>) {
      return cUniqueTable;
    }
  }

  /**
   * @brief Clear all unique tables
   * @see UniqueTable::clear
   * @see RealNumberUniqueTable::clear
   */
  void clearUniqueTables() {
    vUniqueTable.clear();
    mUniqueTable.clear();
    dUniqueTable.clear();
    cUniqueTable.clear();
  }

  /**
   * @brief Increment the reference count of an edge
   * @details This is the main function for increasing reference counts within
   * the DD package. It increases the reference count of the complex edge weight
   * as well as the DD node itself. If the node newly becomes active, meaning
   * that it had a reference count of zero beforehand, the reference count of
   * all children is recursively increased.
   * @tparam Node The node type of the edge.
   * @param e The edge to increase the reference count of
   */
  template <class Node> void incRef(const Edge<Node>& e) noexcept {
    cn.incRef(e.w);
    const auto& p = e.p;
    const auto inc = getUniqueTable<Node>().incRef(p);
    if (inc && p->ref == 1U) {
      for (const auto& child : p->e) {
        incRef(child);
      }
    }
  }

  /**
   * @brief Decrement the reference count of an edge
   * @details This is the main function for decreasing reference counts within
   * the DD package. It decreases the reference count of the complex edge weight
   * as well as the DD node itself. If the node newly becomes dead, meaning
   * that its reference count hit zero, the reference count of all children is
   * recursively decreased.
   * @tparam Node The node type of the edge.
   * @param e The edge to decrease the reference count of
   */
  template <class Node> void decRef(const Edge<Node>& e) noexcept {
    cn.decRef(e.w);
    const auto& p = e.p;
    const auto dec = getUniqueTable<Node>().decRef(p);
    if (dec && p->ref == 0U) {
      for (const auto& child : p->e) {
        decRef(child);
      }
    }
  }

  bool garbageCollect(bool force = false) {
    // return immediately if no table needs collection
    if (!force && !vUniqueTable.possiblyNeedsCollection() &&
        !mUniqueTable.possiblyNeedsCollection() &&
        !dUniqueTable.possiblyNeedsCollection() &&
        !cUniqueTable.possiblyNeedsCollection()) {
      return false;
    }

    auto cCollect = cUniqueTable.garbageCollect(force);
    if (cCollect > 0) {
      // Collecting garbage in the complex numbers table requires collecting the
      // node tables as well
      force = true;
    }
    auto vCollect = vUniqueTable.garbageCollect(force);
    auto mCollect = mUniqueTable.garbageCollect(force);
    auto dCollect = dUniqueTable.garbageCollect(force);

    // invalidate all compute tables involving vectors if any vector node has
    // been collected
    if (vCollect > 0) {
      vectorAdd.clear();
      vectorInnerProduct.clear();
      vectorKronecker.clear();
      matrixVectorMultiplication.clear();
    }
    // invalidate all compute tables involving matrices if any matrix node has
    // been collected
    if (mCollect > 0 || dCollect > 0) {
      matrixAdd.clear();
      matrixTranspose.clear();
      conjugateMatrixTranspose.clear();
      matrixKronecker.clear();
      matrixVectorMultiplication.clear();
      matrixMatrixMultiplication.clear();
      clearIdentityTable();
      stochasticNoiseOperationCache.clear();
      densityAdd.clear();
      densityDensityMultiplication.clear();
      densityNoise.clear();
    }
    // invalidate all compute tables where any component of the entry contains
    // numbers from the complex table if any complex numbers were collected
    if (cCollect > 0) {
      matrixVectorMultiplication.clear();
      matrixMatrixMultiplication.clear();
      matrixTranspose.clear();
      conjugateMatrixTranspose.clear();
      vectorInnerProduct.clear();
      vectorKronecker.clear();
      matrixKronecker.clear();
      stochasticNoiseOperationCache.clear();
      densityAdd.clear();
      densityDensityMultiplication.clear();
      densityNoise.clear();
    }
    return vCollect > 0 || mCollect > 0 || cCollect > 0;
  }

  ///
  /// Vector nodes, edges and quantum states
  ///
  vEdge normalize(const vEdge& e, bool cached) {
    auto zero = std::array{e.p->e[0].w.approximatelyZero(),
                           e.p->e[1].w.approximatelyZero()};

    // make sure to release cached numbers approximately zero, but not exactly
    // zero
    if (cached) {
      for (auto i = 0U; i < RADIX; i++) {
        if (zero[i]) {
          cn.returnToCache(e.p->e[i].w);
          e.p->e[i] = vEdge::zero;
        }
      }
    }

    if (zero[0]) {
      // all equal to zero
      if (zero[1]) {
        if (!cached && !e.isTerminal()) {
          // If it is not a cached computation, the node has to be put back into
          // the chain
          vMemoryManager.returnEntry(e.p);
        }
        return vEdge::zero;
      }

      auto r = e;
      auto& w = r.p->e[1].w;
      if (cached) {
        r.w = w;
      } else {
        r.w = cn.lookup(w);
      }
      w = Complex::one;
      return r;
    }

    if (zero[1]) {
      auto r = e;
      auto& w = r.p->e[0].w;
      if (cached) {
        r.w = w;
      } else {
        r.w = cn.lookup(w);
      }
      w = Complex::one;
      return r;
    }

    const auto mag0 = ComplexNumbers::mag2(e.p->e[0].w);
    const auto mag1 = ComplexNumbers::mag2(e.p->e[1].w);
    const auto norm2 = mag0 + mag1;
    const auto mag2Max = (mag0 + RealNumber::eps >= mag1) ? mag0 : mag1;
    const auto argMax = (mag0 + RealNumber::eps >= mag1) ? 0 : 1;
    const auto norm = std::sqrt(norm2);
    const auto magMax = std::sqrt(mag2Max);
    const auto commonFactor = norm / magMax;

    auto r = e;
    auto& max = r.p->e[static_cast<std::size_t>(argMax)];
    if (cached) {
      if (max.w.exactlyOne()) {
        r.w = cn.lookup(commonFactor, 0.);
      } else {
        r.w = max.w;
        r.w.r->value *= commonFactor;
        r.w.i->value *= commonFactor;
      }
    } else {
      r.w = cn.lookup(RealNumber::val(max.w.r) * commonFactor,
                      RealNumber::val(max.w.i) * commonFactor);
      if (r.w.approximatelyZero()) {
        return vEdge::zero;
      }
    }

    max.w = cn.lookup(magMax / norm, 0.);
    if (max.w == Complex::zero) {
      max = vEdge::zero;
    }

    const auto argMin = (argMax + 1) % 2;
    auto& min = r.p->e[static_cast<std::size_t>(argMin)];
    if (cached) {
      ComplexNumbers::div(min.w, min.w, r.w);
      min.w = cn.lookup(min.w, true);
    } else {
      min.w = cn.lookup(cn.divTemp(min.w, r.w));
    }
    if (min.w == Complex::zero) {
      min = vEdge::zero;
    }

    return r;
  }

  dEdge makeZeroDensityOperator(const std::size_t n) {
    auto f = dEdge::one;
    for (std::size_t p = 0; p < n; p++) {
      f = makeDDNode(static_cast<Qubit>(p),
                     std::array{f, dEdge::zero, dEdge::zero, dEdge::zero});
    }
    return f;
  }

  // generate |0...0> with n qubits
  vEdge makeZeroState(const std::size_t n, const std::size_t start = 0) {
    if (n + start > nqubits) {
      throw std::runtime_error{
          "Requested state with " + std::to_string(n + start) +
          " qubits, but current package configuration only supports up to " +
          std::to_string(nqubits) +
          " qubits. Please allocate a larger package instance."};
    }
    auto f = vEdge::one;
    for (std::size_t p = start; p < n + start; p++) {
      f = makeDDNode(static_cast<Qubit>(p), std::array{f, vEdge::zero});
    }
    return f;
  }
  // generate computational basis state |i> with n qubits
  vEdge makeBasisState(const std::size_t n, const std::vector<bool>& state,
                       const std::size_t start = 0) {
    if (n + start > nqubits) {
      throw std::runtime_error{
          "Requested state with " + std::to_string(n + start) +
          " qubits, but current package configuration only supports up to " +
          std::to_string(nqubits) +
          " qubits. Please allocate a larger package instance."};
    }
    auto f = vEdge::one;
    for (std::size_t p = start; p < n + start; ++p) {
      if (!state[p]) {
        f = makeDDNode(static_cast<Qubit>(p), std::array{f, vEdge::zero});
      } else {
        f = makeDDNode(static_cast<Qubit>(p), std::array{vEdge::zero, f});
      }
    }
    return f;
  }
  // generate general basis state with n qubits
  vEdge makeBasisState(const std::size_t n,
                       const std::vector<BasisStates>& state,
                       const std::size_t start = 0) {
    if (n + start > nqubits) {
      throw std::runtime_error{
          "Requested state with " + std::to_string(n + start) +
          " qubits, but current package configuration only supports up to " +
          std::to_string(nqubits) +
          " qubits. Please allocate a larger package instance."};
    }
    if (state.size() < n) {
      throw std::runtime_error(
          "Insufficient qubit states provided. Requested " + std::to_string(n) +
          ", but received " + std::to_string(state.size()));
    }

    auto f = vEdge::one;
    for (std::size_t p = start; p < n + start; ++p) {
      switch (state[p]) {
      case BasisStates::zero:
        f = makeDDNode(static_cast<Qubit>(p), std::array{f, vEdge::zero});
        break;
      case BasisStates::one:
        f = makeDDNode(static_cast<Qubit>(p), std::array{vEdge::zero, f});
        break;
      case BasisStates::plus:
        f = makeDDNode(
            static_cast<Qubit>(p),
            std::array<vEdge, RADIX>{{{f.p, cn.lookup(dd::SQRT2_2, 0)},
                                      {f.p, cn.lookup(dd::SQRT2_2, 0)}}});
        break;
      case BasisStates::minus:
        f = makeDDNode(
            static_cast<Qubit>(p),
            std::array<vEdge, RADIX>{{{f.p, cn.lookup(dd::SQRT2_2, 0)},
                                      {f.p, cn.lookup(-dd::SQRT2_2, 0)}}});
        break;
      case BasisStates::right:
        f = makeDDNode(
            static_cast<Qubit>(p),
            std::array<vEdge, RADIX>{{{f.p, cn.lookup(dd::SQRT2_2, 0)},
                                      {f.p, cn.lookup(0, dd::SQRT2_2)}}});
        break;
      case BasisStates::left:
        f = makeDDNode(
            static_cast<Qubit>(p),
            std::array<vEdge, RADIX>{{{f.p, cn.lookup(dd::SQRT2_2, 0)},
                                      {f.p, cn.lookup(0, -dd::SQRT2_2)}}});
        break;
      }
    }
    return f;
  }
  // std::vector<std::pair<dd::fp, dd::fp>> makeZeroVector(const std::size_t n, const std::size_t start = 0) {
  //   std::vector<std::pair<dd::fp, dd::fp>> f(n, std::make_pair<dd::fp, dd::fp>(0, 0));
  //   return f;
  // }

  // void makeZeroVector(std::vector<std::pair<dd::fp, dd::fp>> &f, const std::size_t n, const std::size_t start = 0) {
  //   std::fill(f.begin(), f.end(), std::make_pair<dd::fp, dd::fp>(0, 0));
  // }
  // generate the decision diagram from an arbitrary state vector
  vEdge makeStateFromVector(const CVec& stateVector) {
    if (stateVector.empty()) {
      return vEdge::one;
    }
    const auto& length = stateVector.size();
    if ((length & (length - 1)) != 0) {
      throw std::invalid_argument(
          "State vector must have a length of a power of two.");
    }

    if (length == 1) {
      return vEdge::terminal(cn.lookup(stateVector[0]));
    }

    [[maybe_unused]] const auto before = cn.cacheCount();

    const auto level = static_cast<Qubit>(std::log2(length) - 1);
    auto state =
        makeStateFromVector(stateVector.begin(), stateVector.end(), level);

    // the recursive function makes use of the cache, so we have to clean it up
    state.w = cn.lookup(state.w, true);

    [[maybe_unused]] const auto after = cn.cacheCount();
    assert(after == before);
    return state;
  }

  /**
      Converts a given matrix to a decision diagram
      @param matrix A complex matrix to convert to a DD.
      @return An mEdge that represents the DD.
      @throws std::invalid_argument If the given matrix is not square or its
  length is not a power of two.
  **/
  mEdge makeDDFromMatrix(const CMat& matrix) {
    if (matrix.empty()) {
      return mEdge::one;
    }

    const auto& length = matrix.size();
    if ((length & (length - 1)) != 0) {
      throw std::invalid_argument(
          "Matrix must have a length of a power of two.");
    }

    const auto& width = matrix[0].size();
    if (length != width) {
      throw std::invalid_argument("Matrix must be square.");
    }

    if (length == 1) {
      return mEdge::terminal(cn.lookup(matrix[0][0]));
    }

    [[maybe_unused]] const auto before = cn.cacheCount();

    const auto level = static_cast<Qubit>(std::log2(length) - 1);

    auto matrixDD = makeDDFromMatrix(matrix, level, 0, length, 0, width);

    matrixDD.w = cn.lookup(matrixDD.w, true);

    [[maybe_unused]] const auto after = cn.cacheCount();
    assert(after == before);
    return matrixDD;
  }

  ///
  /// Matrix nodes, edges and quantum gates
  ///
  template <class Node> Edge<Node> normalize(const Edge<Node>& e, bool cached) {
    if constexpr (std::is_same_v<Node, mNode> || std::is_same_v<Node, dNode>) {
      auto argmax = -1;

      auto zero = std::array{
          e.p->e[0].w.approximatelyZero(), e.p->e[1].w.approximatelyZero(),
          e.p->e[2].w.approximatelyZero(), e.p->e[3].w.approximatelyZero()};

      // make sure to release cached numbers approximately zero, but not exactly
      // zero
      if (cached) {
        for (auto i = 0U; i < NEDGE; i++) {
          auto& successor = e.p->e[i];
          if (zero[i]) {
            cn.returnToCache(successor.w);
            successor = Edge<Node>::zero;
          }
        }
      }

      fp max = 0;
      auto maxc = Complex::one;
      // determine max amplitude
      for (auto i = 0U; i < NEDGE; ++i) {
        if (zero[i]) {
          continue;
        }
        const auto& w = e.p->e[i].w;
        if (argmax == -1) {
          argmax = static_cast<decltype(argmax)>(i);
          max = ComplexNumbers::mag2(w);
          maxc = w;
        } else {
          auto mag = ComplexNumbers::mag2(w);
          if (mag - max > RealNumber::eps) {
            argmax = static_cast<decltype(argmax)>(i);
            max = mag;
            maxc = w;
          }
        }
      }

      // all equal to zero
      if (argmax == -1) {
        if (!e.isTerminal()) {
          getMemoryManager<Node>().returnEntry(e.p);
        }
        return Edge<Node>::zero;
      }

      auto r = e;
      // divide each entry by max
      for (auto i = 0U; i < NEDGE; ++i) {
        if (static_cast<decltype(argmax)>(i) == argmax) {
          r.p->e[i].w = Complex::one;
          if (r.w.exactlyOne()) {
            r.w = maxc;
            continue;
          }

          if (cached) {
            ComplexNumbers::mul(r.w, r.w, maxc);
          } else {
            r.w = cn.lookup(cn.mulTemp(r.w, maxc));
          }
        } else {
          auto& successor = r.p->e[i];
          if (zero[i]) {
            assert(successor.w.exactlyZero() &&
                   "Should have been set to zero at the start");
            continue;
          }
          // TODO: it might be worth revisiting whether this check actually
          // improves performance or rather causes more instability.
          if (successor.w.approximatelyOne()) {
            if (cached) {
              cn.returnToCache(successor.w);
            }
            successor.w = Complex::one;
          }
          const auto c = cn.divTemp(successor.w, maxc);
          if (cached) {
            cn.returnToCache(successor.w);
          }
          successor.w = cn.lookup(c);
        }
      }
      return r;
    }
  }

  // build matrix representation for a single gate on an n-qubit circuit
  mEdge makeGateDD(const std::array<ComplexValue, NEDGE>& mat,
                   const std::size_t n, const qc::Qubit target,
                   const std::size_t start = 0) {
    return makeGateDD(mat, n, qc::Controls{}, target, start);
  }
  mEdge makeGateDD(const std::array<ComplexValue, NEDGE>& mat,
                   const std::size_t n, const qc::Control& control,
                   const qc::Qubit target, const std::size_t start = 0) {
    return makeGateDD(mat, n, qc::Controls{control}, target, start);
  }
  mEdge makeGateDD(const std::array<ComplexValue, NEDGE>& mat,
                   const std::size_t n, const qc::Controls& controls,
                   const qc::Qubit target, const std::size_t start = 0) {
    if (n + start > nqubits) {
      throw std::runtime_error{
          "Requested gate with " + std::to_string(n + start) +
          " qubits, but current package configuration only supports up to " +
          std::to_string(nqubits) +
          " qubits. Please allocate a larger package instance."};
    }
    std::array<mEdge, NEDGE> em{};
    auto it = controls.begin();
    for (auto i = 0U; i < NEDGE; ++i) {
      // NOLINTNEXTLINE(clang-diagnostic-float-equal) it has to be really zero
      if (mat[i].r == 0 && mat[i].i == 0) {
        em[i] = mEdge::zero;
      } else {
        em[i] = mEdge::terminal(cn.lookup(mat[i]));
      }
    }

    // process lines below target
    auto z = static_cast<Qubit>(start);
    for (; z < static_cast<Qubit>(target); ++z) {
      for (auto i1 = 0U; i1 < RADIX; ++i1) {
        for (auto i2 = 0U; i2 < RADIX; ++i2) {
          auto i = i1 * RADIX + i2;
          if (it != controls.end() && it->qubit == z) {
            auto edges =
                std::array{mEdge::zero, mEdge::zero, mEdge::zero, mEdge::zero};
            if (it->type == qc::Control::Type::Neg) { // neg. control
              edges[0] = em[i];
              if (i1 == i2) {
                if (z == 0U) {
                  edges[3] = mEdge::one;
                } else {
                  edges[3] = makeIdent(start, z - 1U);
                }
              }
            } else { // pos. control
              edges[3] = em[i];
              if (i1 == i2) {
                if (z == 0U) {
                  edges[0] = mEdge::one;
                } else {
                  edges[0] = makeIdent(start, z - 1U);
                }
              }
            }
            em[i] = makeDDNode(z, edges);
          } else { // not connected
            em[i] = makeDDNode(
                z, std::array{em[i], mEdge::zero, mEdge::zero, em[i]});
          }
        }
      }
      if (it != controls.end() && it->qubit == z) {
        ++it;
      }
    }

    // target line
    auto e = makeDDNode(z, em);

    // process lines above target
    for (; z < static_cast<Qubit>(n - 1 + start); z++) {
      auto q = static_cast<Qubit>(z + 1);
      if (it != controls.end() && it->qubit == static_cast<qc::Qubit>(q)) {
        if (it->type == qc::Control::Type::Neg) { // neg. control
          e = makeDDNode(q, std::array{e, mEdge::zero, mEdge::zero,
                                       makeIdent(start, q - 1U)});
        } else { // pos. control
          e = makeDDNode(q, std::array{makeIdent(start, q - 1U), mEdge::zero,
                                       mEdge::zero, e});
        }
        ++it;
      } else { // not connected
        e = makeDDNode(q, std::array{e, mEdge::zero, mEdge::zero, e});
      }
    }
    return e;
  }

  /**
  Creates the DD for a two-qubit gate
  @param mat Matrix representation of the gate
  @param n Number of qubits in the circuit
  @param target0 First target qubit
  @param target1 Second target qubit
  @param start Start index for the DD
  @return DD representing the gate
  @throws std::runtime_error if the number of qubits is larger than the package
  configuration
  **/
  mEdge makeTwoQubitGateDD(
      const std::array<std::array<ComplexValue, NEDGE>, NEDGE>& mat,
      const std::size_t n, const qc::Qubit target0, const qc::Qubit target1,
      const std::size_t start = 0) {
    // sanity check
    if (n + start > nqubits) {
      throw std::runtime_error{
          "Requested gate with " + std::to_string(n + start) +
          " qubits, but current package configuration only supports up to " +
          std::to_string(nqubits) +
          " qubits. Please allocate a larger package instance."};
    }

    // create terminal edge matrix
    std::array<std::array<mEdge, NEDGE>, NEDGE> em{};
    for (auto i1 = 0U; i1 < NEDGE; i1++) {
      const auto& matRow = mat.at(i1);
      auto& emRow = em.at(i1);
      for (auto i2 = 0U; i2 < NEDGE; i2++) {
        const auto& matEntry = matRow.at(i2);
        auto& emEntry = emRow.at(i2);
        // NOLINTNEXTLINE(clang-diagnostic-float-equal) it has to be really zero
        if (matEntry.r == 0 && matEntry.i == 0) {
          emEntry = mEdge::zero;
        } else {
          emEntry = mEdge::terminal(cn.lookup(matEntry));
        }
      }
    }

    // process lines below smaller target (by creating identity structures)
    auto z = static_cast<Qubit>(start);
    const auto smallerTarget = std::min(target0, target1);
    for (; z < smallerTarget; ++z) {
      for (auto& row : em) {
        for (auto& entry : row) {
          entry =
              makeDDNode(z, std::array{entry, mEdge::zero, mEdge::zero, entry});
        }
      }
    }

    // process the smaller target by taking the 16 submatrices and appropriately
    // combining them into four DDs.
    std::array<mEdge, NEDGE> em0{};
    for (std::size_t row = 0; row < RADIX; ++row) {
      for (std::size_t col = 0; col < RADIX; ++col) {
        std::array<mEdge, NEDGE> local{};
        if (target0 > target1) {
          for (std::size_t i = 0; i < RADIX; ++i) {
            for (std::size_t j = 0; j < RADIX; ++j) {
              local.at(i * RADIX + j) =
                  em.at(row * RADIX + i).at(col * RADIX + j);
            }
          }
        } else {
          for (std::size_t i = 0; i < RADIX; ++i) {
            for (std::size_t j = 0; j < RADIX; ++j) {
              local.at(i * RADIX + j) =
                  em.at(i * RADIX + row).at(j * RADIX + col);
            }
          }
        }
        em0.at(row * RADIX + col) = makeDDNode(z, local);
      }
    }

    // process lines between the two targets (by creating identity structures)
    for (++z; z < std::max(target0, target1); ++z) {
      for (auto& entry : em0) {
        entry =
            makeDDNode(z, std::array{entry, mEdge::zero, mEdge::zero, entry});
      }
    }

    // process the larger target by combining the four DDs from the smaller
    // target
    auto e = makeDDNode(z, em0);

    // process lines above the larger target (by creating identity structures)
    const auto end = static_cast<Qubit>(n + start);
    for (++z; z < end; ++z) {
      e = makeDDNode(z, std::array{e, mEdge::zero, mEdge::zero, e});
    }

    return e;
  }

  mEdge makeSWAPDD(const std::size_t n, const qc::Qubit target0,
                   const qc::Qubit target1, const std::size_t start = 0) {
    return makeTwoQubitGateDD(SWAPmat, n, target0, target1, start);
  }
  mEdge makeSWAPDD(const std::size_t n, const qc::Controls& controls,
                   const qc::Qubit target0, const qc::Qubit target1,
                   const std::size_t start = 0) {
    auto c = controls;
    c.insert(qc::Control{target0});
    mEdge e = makeGateDD(Xmat, n, c, target1, start);
    c.erase(qc::Control{target0});
    c.insert(qc::Control{target1});
    e = multiply(e, multiply(makeGateDD(Xmat, n, c, target0, start), e));
    return e;
  }

  mEdge makePeresDD(const std::size_t n, const qc::Controls& controls,
                    const qc::Qubit target0, const qc::Qubit target1,
                    const std::size_t start = 0) {
    auto c = controls;
    c.insert(qc::Control{target1});
    mEdge e = makeGateDD(Xmat, n, c, target0, start);
    e = multiply(makeGateDD(Xmat, n, controls, target1, start), e);
    return e;
  }

  mEdge makePeresdagDD(const std::size_t n, const qc::Controls& controls,
                       const qc::Qubit target0, const qc::Qubit target1,
                       const std::size_t start = 0) {
    mEdge e = makeGateDD(Xmat, n, controls, target1, start);
    auto c = controls;
    c.insert(qc::Control{target1});
    e = multiply(makeGateDD(Xmat, n, c, target0, start), e);
    return e;
  }

  mEdge makeiSWAPDD(const std::size_t n, const qc::Qubit target0,
                    const qc::Qubit target1, const std::size_t start = 0) {
    return makeTwoQubitGateDD(iSWAPmat, n, target0, target1, start);
  }
  mEdge makeiSWAPDD(const std::size_t n, const qc::Controls& controls,
                    const qc::Qubit target0, const qc::Qubit target1,
                    const std::size_t start = 0) {
    mEdge e = makeGateDD(Smat, n, controls, target1, start);        // S q[1]
    e = multiply(e, makeGateDD(Smat, n, controls, target0, start)); // S q[0]
    e = multiply(e, makeGateDD(Hmat, n, controls, target0, start)); // H q[0]
    auto c = controls;
    c.insert(qc::Control{target0});
    e = multiply(e, makeGateDD(Xmat, n, c, target1, start)); // CX q[0], q[1]
    c.erase(qc::Control{target0});
    c.insert(qc::Control{target1});
    e = multiply(e, makeGateDD(Xmat, n, c, target0, start)); // CX q[1], q[0]
    e = multiply(e, makeGateDD(Hmat, n, controls, target1, start)); // H q[1]
    return e;
  }

  mEdge makeiSWAPinvDD(const std::size_t n, const qc::Qubit target0,
                       const qc::Qubit target1, const std::size_t start = 0) {
    return makeTwoQubitGateDD(iSWAPinvmat, n, target0, target1, start);
  }
  mEdge makeiSWAPinvDD(const std::size_t n, const qc::Controls& controls,
                       const qc::Qubit target0, const qc::Qubit target1,
                       const std::size_t start = 0) {
    mEdge e = makeGateDD(Hmat, n, controls, target1, start); // H q[1]
    auto c = controls;
    c.insert(qc::Control{target1});
    e = multiply(e, makeGateDD(Xmat, n, c, target0, start)); // CX q[1], q[0]
    c.erase(qc::Control{target1});
    c.insert(qc::Control{target0});
    e = multiply(e, makeGateDD(Xmat, n, c, target1, start)); // CX q[0], q[1]
    e = multiply(e, makeGateDD(Hmat, n, controls, target0, start)); // H q[0]
    e = multiply(e,
                 makeGateDD(Sdagmat, n, controls, target0, start)); // Sdag q[0]
    e = multiply(e,
                 makeGateDD(Sdagmat, n, controls, target1, start)); // Sdag q[1]
    return e;
  }

  mEdge makeDCXDD(const std::size_t n, const qc::Qubit target0,
                  const qc::Qubit target1, const std::size_t start = 0) {
    return makeTwoQubitGateDD(DCXmat, n, target0, target1, start);
  }
  mEdge makeDCXDD(const std::size_t n, const qc::Controls& controls,
                  const qc::Qubit target0, const qc::Qubit target1,
                  const std::size_t start = 0) {
    auto c = controls;
    c.insert(qc::Control{target0});
    mEdge e = makeGateDD(Xmat, n, c, target1, start);
    c.erase(qc::Control{target0});
    c.insert(qc::Control{target1});
    e = multiply(e, makeGateDD(Xmat, n, c, target0, start));
    return e;
  }

  mEdge makeRZZDD(const std::size_t n, const qc::Qubit target0,
                  const qc::Qubit target1, const fp theta,
                  const std::size_t start = 0) {
    return makeTwoQubitGateDD(RZZmat(theta), n, target0, target1, start);
  }
  mEdge makeRZZDD(const std::size_t n, const qc::Controls& controls,
                  const qc::Qubit target0, const qc::Qubit target1,
                  const fp theta, const std::size_t start = 0) {
    auto c = controls;
    c.insert(qc::Control{target0});
    auto e = makeGateDD(Xmat, n, c, target1, start);
    c.erase(qc::Control{target0});
    e = multiply(e, makeGateDD(RZmat(theta), n, c, target1, start));
    c.insert(qc::Control{target0});
    e = multiply(e, makeGateDD(Xmat, n, c, target1, start));
    return e;
  }

  mEdge makeRYYDD(const std::size_t n, const qc::Qubit target0,
                  const qc::Qubit target1, const fp theta,
                  const std::size_t start = 0) {
    return makeTwoQubitGateDD(RYYmat(theta), n, target0, target1, start);
  }
  mEdge makeRYYDD(const std::size_t n, const qc::Controls& controls,
                  const qc::Qubit target0, const qc::Qubit target1,
                  const fp theta, const std::size_t start = 0) {
    // no controls are necessary on the RX gates since they cancel if the
    // controls are 0.
    auto e = makeGateDD(RXmat(PI_2), n, qc::Controls{}, target0, start);
    e = multiply(e, makeGateDD(RXmat(PI_2), n, qc::Controls{}, target1, start));
    e = multiply(e, makeRZZDD(n, controls, target0, target1, theta, start));
    e = multiply(e,
                 makeGateDD(RXmat(-PI_2), n, qc::Controls{}, target1, start));
    e = multiply(e,
                 makeGateDD(RXmat(-PI_2), n, qc::Controls{}, target0, start));
    return e;
  }

  mEdge makeRXXDD(const std::size_t n, const qc::Qubit target0,
                  const qc::Qubit target1, const fp theta,
                  const std::size_t start = 0) {
    return makeTwoQubitGateDD(RXXmat(theta), n, target0, target1, start);
  }
  mEdge makeRXXDD(const std::size_t n, const qc::Controls& controls,
                  const qc::Qubit target0, const qc::Qubit target1,
                  const fp theta, const std::size_t start = 0) {
    // no controls are necessary on the H gates since they cancel if the
    // controls are 0.
    auto e = makeGateDD(Hmat, n, qc::Controls{}, target0, start);
    e = multiply(e, makeGateDD(Hmat, n, qc::Controls{}, target1, start));
    e = multiply(e, makeRZZDD(n, controls, target0, target1, theta, start));
    e = multiply(e, makeGateDD(Hmat, n, qc::Controls{}, target1, start));
    e = multiply(e, makeGateDD(Hmat, n, qc::Controls{}, target0, start));
    return e;
  }

  mEdge makeRZXDD(const std::size_t n, const qc::Qubit target0,
                  const qc::Qubit target1, const fp theta,
                  const std::size_t start = 0) {
    return makeTwoQubitGateDD(RZXmat(theta), n, target0, target1, start);
  }
  mEdge makeRZXDD(const std::size_t n, const qc::Controls& controls,
                  const qc::Qubit target0, const qc::Qubit target1,
                  const fp theta, const std::size_t start = 0) {
    // no controls are necessary on the H gates since they cancel if the
    // controls are 0.
    auto e = makeGateDD(Hmat, n, qc::Controls{}, target1, start);
    e = multiply(e, makeRZZDD(n, controls, target0, target1, theta, start));
    e = multiply(e, makeGateDD(Hmat, n, qc::Controls{}, target1, start));
    return e;
  }

  mEdge makeECRDD(const std::size_t n, const qc::Qubit target0,
                  const qc::Qubit target1, const std::size_t start = 0) {
    return makeTwoQubitGateDD(ECRmat, n, target0, target1, start);
  }
  mEdge makeECRDD(const std::size_t n, const qc::Controls& controls,
                  const qc::Qubit target0, const qc::Qubit target1,
                  const std::size_t start = 0) {
    auto e = makeRZXDD(n, controls, target0, target1, -PI_4, start);
    e = multiply(e, makeGateDD(Xmat, n, controls, target0, start));
    e = multiply(e, makeRZXDD(n, controls, target0, target1, PI_4, start));
    return e;
  }

  mEdge makeXXMinusYYDD(const std::size_t n, const qc::Qubit target0,
                        const qc::Qubit target1, const fp theta,
                        const fp beta = 0., const std::size_t start = 0) {
    return makeTwoQubitGateDD(XXMinusYYmat(theta, beta), n, target0, target1,
                              start);
  }
  mEdge makeXXMinusYYDD(const std::size_t n, const qc::Controls& controls,
                        const qc::Qubit target0, const qc::Qubit target1,
                        const fp theta, const fp beta = 0.,
                        const std::size_t start = 0) {
    auto e = makeGateDD(RZmat(-beta), n, qc::Controls{}, target1, start);
    e = multiply(e,
                 makeGateDD(RZmat(-PI_2), n, qc::Controls{}, target0, start));
    e = multiply(e, makeGateDD(SXmat, n, qc::Controls{}, target0, start));
    e = multiply(e, makeGateDD(RZmat(PI_2), n, qc::Controls{}, target0, start));
    e = multiply(e, makeGateDD(Smat, n, qc::Controls{}, target1, start));
    e = multiply(e, makeGateDD(Xmat, n, qc::Control{target0}, target1, start));
    // only the following two gates need to be controlled by the controls since
    // the other gates cancel if the controls are 0.
    e = multiply(e,
                 makeGateDD(RYmat(-theta / 2.), n, controls, target0, start));
    e = multiply(e, makeGateDD(RYmat(theta / 2.), n, controls, target1, start));

    e = multiply(e, makeGateDD(Xmat, n, qc::Control{target0}, target1, start));
    e = multiply(e, makeGateDD(Sdagmat, n, qc::Controls{}, target1, start));
    e = multiply(e,
                 makeGateDD(RZmat(-PI_2), n, qc::Controls{}, target0, start));
    e = multiply(e, makeGateDD(SXdagmat, n, qc::Controls{}, target0, start));
    e = multiply(e, makeGateDD(RZmat(PI_2), n, qc::Controls{}, target0, start));
    e = multiply(e, makeGateDD(RZmat(beta), n, qc::Controls{}, target1, start));
    return e;
  }

  mEdge makeXXPlusYYDD(const std::size_t n, const qc::Qubit target0,
                       const qc::Qubit target1, const fp theta,
                       const fp beta = 0., const std::size_t start = 0) {
    return makeTwoQubitGateDD(XXPlusYYmat(theta, beta), n, target0, target1,
                              start);
  }
  mEdge makeXXPlusYYDD(const std::size_t n, const qc::Controls& controls,
                       const qc::Qubit target0, const qc::Qubit target1,
                       const fp theta, const fp beta = 0.,
                       const std::size_t start = 0) {
    auto e = makeGateDD(RZmat(beta), n, qc::Controls{}, target1, start);
    e = multiply(e,
                 makeGateDD(RZmat(-PI_2), n, qc::Controls{}, target0, start));
    e = multiply(e, makeGateDD(SXmat, n, qc::Controls{}, target0, start));
    e = multiply(e, makeGateDD(RZmat(PI_2), n, qc::Controls{}, target0, start));
    e = multiply(e, makeGateDD(Smat, n, qc::Controls{}, target1, start));
    e = multiply(e, makeGateDD(Xmat, n, qc::Control{target0}, target1, start));
    // only the following two gates need to be controlled by the controls since
    // the other gates cancel if the controls are 0.
    e = multiply(e, makeGateDD(RYmat(theta / 2.), n, controls, target0, start));
    e = multiply(e, makeGateDD(RYmat(theta / 2.), n, controls, target1, start));

    e = multiply(e, makeGateDD(Xmat, n, qc::Control{target0}, target1, start));
    e = multiply(e, makeGateDD(Sdagmat, n, qc::Controls{}, target1, start));
    e = multiply(e,
                 makeGateDD(RZmat(-PI_2), n, qc::Controls{}, target0, start));
    e = multiply(e, makeGateDD(SXdagmat, n, qc::Controls{}, target0, start));
    e = multiply(e, makeGateDD(RZmat(PI_2), n, qc::Controls{}, target0, start));
    e = multiply(e,
                 makeGateDD(RZmat(-beta), n, qc::Controls{}, target1, start));
    return e;
  }

private:
  // check whether node represents a symmetric matrix or the identity
  void checkSpecialMatrices(mNode* p) {
    if (mNode::isTerminal(p)) {
      return;
    }

    p->setIdentity(false);
    p->setSymmetric(false);

    // check if matrix is symmetric
    const auto& e0 = p->e[0];
    const auto& e3 = p->e[3];
    if (!mNode::isSymmetric(e0.p) || !mNode::isSymmetric(e3.p)) {
      return;
    }
    if (transpose(p->e[1]) != p->e[2]) {
      return;
    }
    p->setSymmetric(true);

    // check if matrix resembles identity
    const auto& e1 = p->e[1];
    const auto& e2 = p->e[2];
    if (!mNode::isIdentity(e0.p) || !e1.w.exactlyZero() ||
        !e2.w.exactlyZero() || !e0.w.exactlyOne() || !e3.w.exactlyOne() ||
        !mNode::isIdentity(e3.p)) {
      return;
    }
    p->setIdentity(true);
  }

  vEdge makeStateFromVector(const CVec::const_iterator& begin,
                            const CVec::const_iterator& end,
                            const Qubit level) {
    if (level == 0U) {
      assert(std::distance(begin, end) == 2);
      const auto& zeroWeight = cn.getCached(*begin);
      const auto& oneWeight = cn.getCached(*std::next(begin));
      const auto zeroSuccessor = vEdge{vNode::getTerminal(), zeroWeight};
      const auto oneSuccessor = vEdge{vNode::getTerminal(), oneWeight};
      return makeDDNode<vNode>(0, {zeroSuccessor, oneSuccessor}, true);
    }

    const auto half = std::distance(begin, end) / 2;
    const auto zeroSuccessor =
        makeStateFromVector(begin, begin + half, level - 1);
    const auto oneSuccessor = makeStateFromVector(begin + half, end, level - 1);
    return makeDDNode<vNode>(level, {zeroSuccessor, oneSuccessor}, true);
  }

  /**
  Constructs a decision diagram (DD) from a complex matrix using a recursive
  algorithm.
  @param matrix The complex matrix from which to create the DD.
  @param level The current level of recursion. Starts at the highest level of
  the matrix (log base 2 of the matrix size - 1).
  @param rowStart The starting row of the quadrant being processed.
  @param rowEnd The ending row of the quadrant being processed.
  @param colStart The starting column of the quadrant being processed.
  @param colEnd The ending column of the quadrant being processed.
  @return An mEdge representing the root node of the created DD.
  @details This function recursively breaks down the matrix into quadrants until
  each quadrant has only one element. At each level of recursion, four new edges
  are created, one for each quadrant of the matrix. The four resulting decision
  diagram edges are used to create a new decision diagram node at the current
  level, and this node is returned as the result of the current recursive call.
  At the base case of recursion, the matrix has only one element, which is
  converted into a terminal node of the decision diagram.
  @note This function assumes that the matrix size is a power of two.
  **/
  mEdge makeDDFromMatrix(const CMat& matrix, const Qubit level,
                         const std::size_t rowStart, const std::size_t rowEnd,
                         const std::size_t colStart, const std::size_t colEnd) {
    // base case
    if (level == 0U) {
      assert(rowEnd - rowStart == 2);
      assert(colEnd - colStart == 2);
      const auto w0 = cn.getCached(matrix[rowStart][colStart]);
      const auto e0 = mEdge{mNode::getTerminal(), w0};
      const auto w1 = cn.getCached(matrix[rowStart + 1][colStart]);
      const auto e1 = mEdge{mNode::getTerminal(), w1};
      const auto w2 = cn.getCached(matrix[rowStart][colStart + 1]);
      const auto e2 = mEdge{mNode::getTerminal(), w2};
      const auto w3 = cn.getCached(matrix[rowStart + 1][colStart + 1]);
      const auto e3 = mEdge{mNode::getTerminal(), w3};
      return makeDDNode<mNode>(0U, {e0, e1, e2, e3}, true);
    }

    // recursively call the function on all quadrants
    const auto rowMid = (rowStart + rowEnd) / 2;
    const auto colMid = (colStart + colEnd) / 2;

    const auto edge0 =
        makeDDFromMatrix(matrix, level - 1, rowStart, rowMid, colStart, colMid);
    const auto edge1 =
        makeDDFromMatrix(matrix, level - 1, rowStart, rowMid, colMid, colEnd);
    const auto edge2 =
        makeDDFromMatrix(matrix, level - 1, rowMid, rowEnd, colStart, colMid);
    const auto edge3 =
        makeDDFromMatrix(matrix, level - 1, rowMid, rowEnd, colMid, colEnd);

    return makeDDNode<mNode>(level, {edge0, edge1, edge2, edge3}, true);
  }

public:
  // create a normalized DD node and return an edge pointing to it. The node is
  // not recreated if it already exists.
  template <class Node>
  Edge<Node> makeDDNode(
      const Qubit var,
      const std::array<Edge<Node>, std::tuple_size_v<decltype(Node::e)>>& edges,
      const bool cached = false,
      [[maybe_unused]] const bool generateDensityMatrix = false) {
    auto& memoryManager = getMemoryManager<Node>();
    Edge<Node> e{memoryManager.get(), Complex::one};
    e.p->v = var;
    e.p->e = edges;

    if constexpr (std::is_same_v<Node, mNode> || std::is_same_v<Node, dNode>) {
      e.p->flags = 0;
      if constexpr (std::is_same_v<Node, dNode>) {
        e.p->setDensityMatrixNodeFlag(generateDensityMatrix);
      }
    }

    assert(e.p->ref == 0);
    for ([[maybe_unused]] const auto& edge : edges) {
      // an error here indicates that cached nodes are assigned multiple times.
      // Check if garbage collect correctly resets the cache tables!
      assert(edge.isTerminal() || edge.p->v == var - 1);
    }

    // normalize it
    e = normalize(e, cached);
    assert(e.isTerminal() || e.p->v == var);

    // look it up in the unique tables
    auto& uniqueTable = getUniqueTable<Node>();
    auto l = uniqueTable.lookup(e, false);
    assert(l.isTerminal() || l.p->v == var);

    // set specific node properties for matrices
    if constexpr (std::is_same_v<Node, mNode>) {
      if (l.p == e.p) {
        checkSpecialMatrices(l.p);
      }
    }

    return l;
  }

  template <class Node>
  Edge<Node> deleteEdge(const Edge<Node>& e, const Qubit v,
                        const std::size_t edgeIdx) {
    std::unordered_map<Node*, Edge<Node>> nodes{};
    return deleteEdge(e, v, edgeIdx, nodes);
  }

private:
  template <class Node>
  Edge<Node> deleteEdge(const Edge<Node>& e, const Qubit v,
                        const std::size_t edgeIdx,
                        std::unordered_map<Node*, Edge<Node>>& nodes) {
    if (e.isTerminal()) {
      return e;
    }

    const auto& nodeit = nodes.find(e.p);
    Edge<Node> newedge{};
    if (nodeit != nodes.end()) {
      newedge = nodeit->second;
    } else {
      constexpr std::size_t n = std::tuple_size_v<decltype(e.p->e)>;
      std::array<Edge<Node>, n> edges{};
      if (e.p->v == v) {
        for (std::size_t i = 0; i < n; i++) {
          edges[i] = i == edgeIdx
                         ? Edge<Node>::zero
                         : e.p->e[i]; // optimization -> node cannot occur below
                                      // again, since dd is assumed to be free
        }
      } else {
        for (std::size_t i = 0; i < n; i++) {
          edges[i] = deleteEdge(e.p->e[i], v, edgeIdx, nodes);
        }
      }

      newedge = makeDDNode(e.p->v, edges);
      nodes[e.p] = newedge;
    }

    if (newedge.w.approximatelyOne()) {
      newedge.w = e.w;
    } else {
      newedge.w = cn.lookup(cn.mulTemp(newedge.w, e.w));
    }

    return newedge;
  }

  ///
  /// Compute table definitions
  ///
public:
  void clearComputeTables() {
    vectorAdd.clear();
    matrixAdd.clear();
    matrixTranspose.clear();
    conjugateMatrixTranspose.clear();
    matrixMatrixMultiplication.clear();
    matrixVectorMultiplication.clear();
    vectorInnerProduct.clear();
    vectorKronecker.clear();
    matrixKronecker.clear();

    clearIdentityTable();

    stochasticNoiseOperationCache.clear();
    densityAdd.clear();
    densityDensityMultiplication.clear();
    densityNoise.clear();
  }

  ///
  /// Measurements from state decision diagrams
  ///
  std::string measureAll(vEdge& rootEdge, const bool collapse,
                         std::mt19937_64& mt, const fp epsilon = 0.001) {
    if (std::abs(ComplexNumbers::mag2(rootEdge.w) - 1.0) > epsilon) {
      if (rootEdge.w.approximatelyZero()) {
        throw std::runtime_error(
            "Numerical instabilities led to a 0-vector! Abort simulation!");
      }
      std::cerr << "WARNING in MAll: numerical instability occurred during "
                   "simulation: |alpha|^2 + |beta|^2 = "
                << ComplexNumbers::mag2(rootEdge.w) << ", but should be 1!\n";
    }

    if (rootEdge.isTerminal()) {
      return "";
    }

    vEdge cur = rootEdge;
    const auto numberOfQubits = static_cast<std::size_t>(rootEdge.p->v) + 1U;

    std::string result(numberOfQubits, '0');

    std::uniform_real_distribution<fp> dist(0.0, 1.0L);

    for (auto i = numberOfQubits; i > 0; --i) {
      fp p0 = ComplexNumbers::mag2(cur.p->e.at(0).w);
      const fp p1 = ComplexNumbers::mag2(cur.p->e.at(1).w);
      const fp tmp = p0 + p1;

      if (std::abs(tmp - 1.0) > epsilon) {
        throw std::runtime_error("Added probabilities differ from 1 by " +
                                 std::to_string(std::abs(tmp - 1.0)));
      }
      p0 /= tmp;

      const fp threshold = dist(mt);
      if (threshold < p0) {
        cur = cur.p->e.at(0);
      } else {
        result[cur.p->v] = '1';
        cur = cur.p->e.at(1);
      }
    }

    if (collapse) {
      decRef(rootEdge);

      vEdge e = vEdge::one;
      std::array<vEdge, 2> edges{};

      for (std::size_t p = 0U; p < numberOfQubits; ++p) {
        if (result[p] == '0') {
          edges[0] = e;
          edges[1] = vEdge::zero;
        } else {
          edges[0] = vEdge::zero;
          edges[1] = e;
        }
        e = makeDDNode(static_cast<Qubit>(p), edges, false);
      }
      incRef(e);
      rootEdge = e;
      garbageCollect();
    }

    return std::string{result.rbegin(), result.rend()};
  }

private:
  double assignProbabilities(const vEdge& edge,
                             std::unordered_map<const vNode*, fp>& probs) {
    auto it = probs.find(edge.p);
    if (it != probs.end()) {
      return ComplexNumbers::mag2(edge.w) * it->second;
    }
    double sum{1};
    if (!edge.isTerminal()) {
      sum = assignProbabilities(edge.p->e[0], probs) +
            assignProbabilities(edge.p->e[1], probs);
    }

    probs.insert({edge.p, sum});

    return ComplexNumbers::mag2(edge.w) * sum;
  }

public:
  std::pair<dd::fp, dd::fp>
  determineMeasurementProbabilities(const vEdge& rootEdge, const Qubit index,
                                    const bool assumeProbabilityNormalization) {
    std::map<const vNode*, fp> probsMone;
    std::set<const vNode*> visited;
    std::queue<const vNode*> q;

    probsMone[rootEdge.p] = ComplexNumbers::mag2(rootEdge.w);
    visited.insert(rootEdge.p);
    q.push(rootEdge.p);

    while (q.front()->v != index) {
      const auto* ptr = q.front();
      q.pop();
      const fp prob = probsMone[ptr];

      const auto& s0 = ptr->e[0];
      if (!s0.w.approximatelyZero()) {
        const fp tmp1 = prob * ComplexNumbers::mag2(s0.w);

        if (visited.find(s0.p) != visited.end()) {
          probsMone[s0.p] = probsMone[s0.p] + tmp1;
        } else {
          probsMone[s0.p] = tmp1;
          visited.insert(s0.p);
          q.push(s0.p);
        }
      }

      const auto& s1 = ptr->e[1];
      if (!s1.w.approximatelyZero()) {
        const fp tmp1 = prob * ComplexNumbers::mag2(s1.w);

        if (visited.find(s1.p) != visited.end()) {
          probsMone[s1.p] = probsMone[s1.p] + tmp1;
        } else {
          probsMone[s1.p] = tmp1;
          visited.insert(s1.p);
          q.push(s1.p);
        }
      }
    }

    fp pzero{0};
    fp pone{0};

    if (assumeProbabilityNormalization) {
      while (!q.empty()) {
        const auto* ptr = q.front();
        q.pop();
        const auto& s0 = ptr->e[0];
        if (!s0.w.approximatelyZero()) {
          pzero += probsMone[ptr] * ComplexNumbers::mag2(s0.w);
        }
        const auto& s1 = ptr->e[1];
        if (!s1.w.approximatelyZero()) {
          pone += probsMone[ptr] * ComplexNumbers::mag2(s1.w);
        }
      }
    } else {
      std::unordered_map<const vNode*, fp> probs;
      assignProbabilities(rootEdge, probs);

      while (!q.empty()) {
        const auto* ptr = q.front();
        q.pop();

        const auto& s0 = ptr->e[0];
        if (!s0.w.approximatelyZero()) {
          pzero += probsMone[ptr] * probs[s0.p] * ComplexNumbers::mag2(s0.w);
        }
        const auto& s1 = ptr->e[1];
        if (!s1.w.approximatelyZero()) {
          pone += probsMone[ptr] * probs[s1.p] * ComplexNumbers::mag2(s1.w);
        }
      }
    }
    return {pzero, pone};
  }

  /**
   * @brief Measures the qubit with the given index in the given state vector
   * decision diagram. Collapses the state according to the measurement result.
   * @param rootEdge the root edge of the state vector decision diagram
   * @param index the index of the qubit to be measured
   * @param assumeProbabilityNormalization whether or not to assume that the
   * state vector decision diagram has normalized edge weights.
   * @param mt the random number generator
   * @param epsilon the numerical precision used for checking the normalization
   * of the state vector decision diagram
   * @return the measurement result ('0' or '1')
   * @throws std::runtime_error if a numerical instability is detected during
   * the measurement.
   */
  char measureOneCollapsing(vEdge& rootEdge, const Qubit index,
                            const bool assumeProbabilityNormalization,
                            std::mt19937_64& mt, const fp epsilon = 0.001) {
    const auto& [pzero, pone] = determineMeasurementProbabilities(
        rootEdge, index, assumeProbabilityNormalization);
    const fp sum = pzero + pone;
    if (std::abs(sum - 1) > epsilon) {
      throw std::runtime_error(
          "Numerical instability occurred during measurement: |alpha|^2 + "
          "|beta|^2 = " +
          std::to_string(pzero) + " + " + std::to_string(pone) + " = " +
          std::to_string(pzero + pone) + ", but should be 1!");
    }
    std::uniform_real_distribution<fp> dist(0., 1.);
    if (const auto threshold = dist(mt); threshold < pzero / sum) {
      performCollapsingMeasurement(rootEdge, index, pzero, true);
      return '0';
    }
    performCollapsingMeasurement(rootEdge, index, pone, false);
    return '1';
  }

  /**
   * @brief Performs a specific measurement on the given state vector decision
   * diagram. Collapses the state according to the measurement result.
   * @param rootEdge the root edge of the state vector decision diagram
   * @param index the index of the qubit to be measured
   * @param probability the probability of the measurement result (required for
   * normalization)
   * @param measureZero whether or not to measure '0' (otherwise '1' is
   * measured)
   */
  void performCollapsingMeasurement(vEdge& rootEdge, const Qubit index,
                                    const fp probability,
                                    const bool measureZero) {
    GateMatrix measurementMatrix{complex_zero, complex_zero, complex_zero,
                                 complex_zero};

    if (measureZero) {
      measurementMatrix[0] = complex_one;
    } else {
      measurementMatrix[3] = complex_one;
    }

    const auto measurementGate =
        makeGateDD(measurementMatrix, rootEdge.p->v + 1U, index);

    vEdge e = multiply(measurementGate, rootEdge);

    assert(probability > 0.);
    Complex c = cn.getTemporary(std::sqrt(1.0 / probability), 0);
    ComplexNumbers::mul(c, e.w, c);
    e.w = cn.lookup(c);
    incRef(e);
    decRef(rootEdge);
    rootEdge = e;
  }

  ///
  /// Addition
  ///
  ComputeTable<vCachedEdge, vCachedEdge, vCachedEdge,
               Config::CT_VEC_ADD_NBUCKET>
      vectorAdd{};
  ComputeTable<mCachedEdge, mCachedEdge, mCachedEdge,
               Config::CT_MAT_ADD_NBUCKET>
      matrixAdd{};
  ComputeTable<dCachedEdge, dCachedEdge, dCachedEdge, Config::CT_DM_ADD_NBUCKET>
      densityAdd{};

  template <class Node> [[nodiscard]] auto& getAddComputeTable() {
    if constexpr (std::is_same_v<Node, vNode>) {
      return vectorAdd;
    } else if constexpr (std::is_same_v<Node, mNode>) {
      return matrixAdd;
    } else if constexpr (std::is_same_v<Node, dNode>) {
      return densityAdd;
    }
  }

  template <class Edge> Edge add(const Edge& x, const Edge& y) {
    [[maybe_unused]] const auto before = cn.cacheCount();

    auto result = add2(x, y);
    result.w = cn.lookup(result.w, true);

    [[maybe_unused]] const auto after = cn.cacheCount();
    assert(after == before);

    return result;
  }

  template <class Node>
  Edge<Node> add2(const Edge<Node>& x, const Edge<Node>& y) {
    if (x.w.exactlyZero()) {
      if (y.w.exactlyZero()) {
        return Edge<Node>::zero;
      }
      return {y.p, cn.getCached(y.w)};
    }
    if (y.w.exactlyZero()) {
      return {x.p, cn.getCached(x.w)};
    }
    if (x.p == y.p) {
      auto r = y;
      r.w = cn.addCached(x.w, y.w);
      if (r.w.approximatelyZero()) {
        cn.returnToCache(r.w);
        return Edge<Node>::zero;
      }
      return r;
    }

    auto& computeTable = getAddComputeTable<Node>();
    if (const auto* r = computeTable.lookup({x.p, x.w}, {y.p, y.w});
        r != nullptr) {
      if (r->w.approximatelyZero()) {
        return Edge<Node>::zero;
      }
      return {r->p, cn.getCached(r->w)};
    }
    add_cnt++;
    const Qubit w = (x.isTerminal() || (!y.isTerminal() && y.p->v > x.p->v))
                        ? y.p->v
                        : x.p->v;

    constexpr std::size_t n = std::tuple_size_v<decltype(x.p->e)>;
    std::array<Edge<Node>, n> edge{};
    for (std::size_t i = 0U; i < n; i++) {
      Edge<Node> e1{};
      if (!x.isTerminal() && x.p->v == w) {
        e1 = x.p->e[i];

        if (!e1.w.exactlyZero()) {
          e1.w = cn.mulCached(e1.w, x.w);
        }
      } else {
        e1 = x;
        if (y.p->e[i].isTerminal()) {
          e1 = Edge<Node>::zero;
        }
      }
      Edge<Node> e2{};
      if (!y.isTerminal() && y.p->v == w) {
        e2 = y.p->e[i];

        if (!e2.w.exactlyZero()) {
          e2.w = cn.mulCached(e2.w, y.w);
        }
      } else {
        e2 = y;
        if (x.p->e[i].isTerminal()) {
          e2 = Edge<Node>::zero;
        }
      }

      if constexpr (std::is_same_v<Node, dNode>) {
        dEdge::applyDmChangesToEdges(e1, e2);
        edge[i] = add2(e1, e2);
        dEdge::revertDmChangesToEdges(e1, e2);
      } else {
        edge[i] = add2(e1, e2);
      }

      if (!x.isTerminal() && x.p->v == w) {
        cn.returnToCache(e1.w);
      }

      if (!y.isTerminal() && y.p->v == w) {
        cn.returnToCache(e2.w);
      }
    }

    auto e = makeDDNode(w, edge, true);

    computeTable.insert({x.p, x.w}, {y.p, y.w}, {e.p, e.w});
    return e;
  }

  ///
  /// Matrix (conjugate) transpose
  ///
  UnaryComputeTable<mEdge, mEdge, Config::CT_MAT_TRANS_NBUCKET>
      matrixTranspose{};
  UnaryComputeTable<mEdge, mEdge, Config::CT_MAT_CONJ_TRANS_NBUCKET>
      conjugateMatrixTranspose{};

  mEdge transpose(const mEdge& a) {
    if (a.isTerminal() || a.p->isSymmetric()) {
      return a;
    }

    // check in compute table
    if (const auto* r = matrixTranspose.lookup(a); r != nullptr) {
      return *r;
    }

    std::array<mEdge, NEDGE> e{};
    // transpose sub-matrices and rearrange as required
    for (auto i = 0U; i < RADIX; ++i) {
      for (auto j = 0U; j < RADIX; ++j) {
        e[RADIX * i + j] = transpose(a.p->e[RADIX * j + i]);
      }
    }
    // create new top node
    auto res = makeDDNode(a.p->v, e);
    // adjust top weight
    res.w = cn.lookup(cn.mulTemp(res.w, a.w));

    // put in compute table
    matrixTranspose.insert(a, res);
    return res;
  }
  mEdge conjugateTranspose(const mEdge& a) {
    if (a.isTerminal()) { // terminal case
      return {a.p, ComplexNumbers::conj(a.w)};
    }

    // check if in compute table
    if (const auto* r = conjugateMatrixTranspose.lookup(a); r != nullptr) {
      return *r;
    }

    std::array<mEdge, NEDGE> e{};
    // conjugate transpose submatrices and rearrange as required
    for (auto i = 0U; i < RADIX; ++i) {
      for (auto j = 0U; j < RADIX; ++j) {
        e[RADIX * i + j] = conjugateTranspose(a.p->e[RADIX * j + i]);
      }
    }
    // create new top node
    auto res = makeDDNode(a.p->v, e);

    // adjust top weight including conjugate
    res.w = cn.lookup(cn.mulTemp(res.w, ComplexNumbers::conj(a.w)));

    // put it in the compute table
    conjugateMatrixTranspose.insert(a, res);
    return res;
  }

  ///
  /// Multiplication
  ///
  ComputeTable<mEdge, vEdge, vCachedEdge, Config::CT_MAT_VEC_MULT_NBUCKET>
      matrixVectorMultiplication{};
  ComputeTable<mEdge, mEdge, mCachedEdge, Config::CT_MAT_MAT_MULT_NBUCKET>
      matrixMatrixMultiplication{};
  ComputeTable<dEdge, dEdge, dCachedEdge, Config::CT_DM_DM_MULT_NBUCKET>
      densityDensityMultiplication{};

  template <class LeftOperandNode, class RightOperandNode>
  [[nodiscard]] auto& getMultiplicationComputeTable() {
    if constexpr (std::is_same_v<RightOperandNode, vNode>) {
      return matrixVectorMultiplication;
    } else if constexpr (std::is_same_v<RightOperandNode, mNode>) {
      return matrixMatrixMultiplication;
    } else if constexpr (std::is_same_v<RightOperandNode, dNode>) {
      return densityDensityMultiplication;
    }
  }

  dEdge applyOperationToDensity(dEdge& e, const mEdge& operation,
                                const bool generateDensityMatrix = false) {
    [[maybe_unused]] const auto before = cn.cacheCount();
    auto tmp0 = conjugateTranspose(operation);
    auto tmp1 = multiply(e, densityFromMatrixEdge(tmp0), 0, false);
    auto tmp2 = multiply(densityFromMatrixEdge(operation), tmp1, 0,
                         generateDensityMatrix);
    incRef(tmp2);
    dEdge::alignDensityEdge(e);
    decRef(e);
    e = tmp2;

    if (generateDensityMatrix) {
      dEdge::setDensityMatrixTrue(e);
    }

    return e;
  }
  // void nodeEdgeCount( Edge<mNode>& x , int &node_num, size_t &red_ind, int qubit_num) {
  //   std::set<mNode*> visited;
  //   std::vector<size_t> red_lv;
  //   node_num = 0;
  //   red_ind = 0;
  //   for (int lv = 0; lv < qubit_num; lv++) { red_lv.push_back(0); }
  //   std::function<int (mNode* node)> DFSFunc = [&] (mNode* node) {
  //     int node_cnt = 0;
  //     if (node == nullptr) {return 0;}
  //     if (visited.find(node) == visited.end()) {
  //       constexpr std::size_t rows = RADIX;
  //       constexpr std::size_t cols = RADIX;
  //       for (int i = 0; i < rows; ++i) {
  //         for (int j = 0; j < cols; j++)
  //         {
  //           if (!node->e[i*cols+j].w.exactlyZero())
  //           {
  //             node_cnt += DFSFunc(node->e[i*cols+j].p);
  //           }
  //         }
  //       }
  //       for (size_t i = 0; i < cols; i++)
  //       {
  //         if (node->e[0+i].p != nullptr && node->e[0+i].p == node->e[cols+i].p) {
  //           red_lv[node->v]++;
  //         }
  //       }

  //       visited.insert(node);
  //       return 1+node_cnt;
  //     }
  //     else {
  //       return 0;
  //     }

  //   };
  //   node_num = DFSFunc(x.p);
    
  //   for (int lv = 0; lv < qubit_num; lv++) { red_ind += std::pow(2, lv) * red_lv[lv]; }
  // }

  template <class LeftOperand, class RightOperand>
  RightOperand
  multiply(const LeftOperand& x, const RightOperand& y, const Qubit start = 0,
           [[maybe_unused]] const bool generateDensityMatrix = false) {
    static_assert(std::disjunction_v<std::is_same<LeftOperand, mEdge>,
                                     std::is_same<LeftOperand, dEdge>>,
                  "Left operand must be a matrix or density matrix");
    static_assert(std::disjunction_v<std::is_same<RightOperand, vEdge>,
                                     std::is_same<RightOperand, mEdge>,
                                     std::is_same<RightOperand, dEdge>>,
                  "Right operand must be a vector, matrix or density matrix");
    [[maybe_unused]] const auto before = cn.cacheCount();
    mul_cnt = 0;
    add_cnt = 0;
    Qubit var{};
    RightOperand e;
    if constexpr (std::is_same_v<LeftOperand, dEdge>) {
      auto xCopy = x;
      auto yCopy = y;
      dEdge::applyDmChangesToEdges(xCopy, yCopy);

      if (!xCopy.isTerminal()) {
        var = xCopy.p->v;
      }
      if (!y.isTerminal() && yCopy.p->v > var) {
        var = yCopy.p->v;
      }

      e = multiply2(xCopy, yCopy, var, start, generateDensityMatrix);
      dEdge::revertDmChangesToEdges(xCopy, yCopy);
    } else {
      if (!x.isTerminal()) {
        assert(x.p != nullptr);
        var = x.p->v;
      }
      if (!y.isTerminal() && (y.p->v) > var) {
        assert(y.p != nullptr);
        var = y.p->v;
      }
      e = multiply2(x, y, var, start);
    }

    e.w = cn.lookup(e.w, true);
    [[maybe_unused]] const auto after = cn.cacheCount();
    assert(before == after);
    return e;
  }

template <class LeftOperand>
  void 
  DDArrMultiplyIP(const LeftOperand& x, 
      const dd::fp* y_real, const dd::fp* y_imag, 
      dd::fp* z_real, dd::fp* z_imag, 
      const size_t nDim) {
    static_assert(std::disjunction_v<std::is_same<LeftOperand, mEdge>>,
                  "Left operand must be a matrix");

    [[maybe_unused]] const auto before = cn.cacheCount();

    Qubit var{};

    // first instr.
    if constexpr (std::is_same_v<LeftOperand, mEdge>) {
      if (!x.isTerminal()) {
        assert(x.p != nullptr);
        var = x.p->v;
      }
      DDArrMultiplyIP2(x, y_real, y_imag, z_real, z_imag, var, 0, nDim , 0);
    }

    // e1.w = cn.lookup(e1.w, true); //

    [[maybe_unused]] const auto after = cn.cacheCount();
    assert(before == after);

    return;
  }


template <class LeftOperand>
  void 
  DDArrMultiplyOP(const LeftOperand& x, 
    const dd::fp* y_real, const dd::fp* y_imag, 
    dd::fp* z_real, dd::fp* z_imag, 
            const size_t nDim, std::vector<dd::fp*> z_real_vec, std::vector<dd::fp*> z_imag_vec) {
    static_assert(std::disjunction_v<std::is_same<LeftOperand, mEdge>>,
                  "Left operand must be a matrix");

    [[maybe_unused]] const auto before = cn.cacheCount();

    Qubit var{};

    // first instr.
    if constexpr (std::is_same_v<LeftOperand, mEdge>) {
      if (!x.isTerminal()) {
        assert(x.p != nullptr);
        var = x.p->v;
      }
      DDArrMultiplyOP2(x, y_real, y_imag, z_real, z_imag, var, 0, nDim , 0, z_real_vec, z_imag_vec);
    }

    // e1.w = cn.lookup(e1.w, true); //

    [[maybe_unused]] const auto after = cn.cacheCount();
    assert(before == after);

    return;
  }


private:
  template <class LeftOperandNode, class RightOperandNode>
  Edge<RightOperandNode>
  multiply2(const Edge<LeftOperandNode>& x, const Edge<RightOperandNode>& y,
            const Qubit var, const Qubit start = 0,
            [[maybe_unused]] const bool generateDensityMatrix = false) {
    using LEdge = Edge<LeftOperandNode>;
    using REdge = Edge<RightOperandNode>;
    using ResultEdge = Edge<RightOperandNode>;

    if (x.w.exactlyZero() || y.w.exactlyZero()) {
      return ResultEdge::zero;
    }

    if (x.isTerminal() && y.isTerminal()) {
      return ResultEdge::terminal(cn.mulCached(x.w, y.w));
    }
    auto xCopy = LEdge{x.p, Complex::one};
    auto yCopy = REdge{y.p, Complex::one};
    mul_cnt++;
    auto& computeTable =
        getMultiplicationComputeTable<LeftOperandNode, RightOperandNode>();
    if (const auto* r =
            computeTable.lookup(xCopy, yCopy, generateDensityMatrix);
        r != nullptr) {
      if (r->w.approximatelyZero()) {
        return ResultEdge::zero;
      }
      auto e = ResultEdge{r->p, cn.getCached(r->w)};
      ComplexNumbers::mul(e.w, e.w, x.w);
      ComplexNumbers::mul(e.w, e.w, y.w);
      if (e.w.approximatelyZero()) {
        cn.returnToCache(e.w);
        return ResultEdge::zero;
      }
      return e;
    }
    constexpr std::size_t n = std::tuple_size_v<decltype(y.p->e)>;
    ResultEdge e{};
    if constexpr (std::is_same_v<RightOperandNode, mCachedEdge>) {
      // This branch is only taken for matrices
      if (x.p->v == var && x.p->v == y.p->v) {
        if (x.p->isIdentity()) {
          if constexpr (n == NEDGE) {
            // additionally check if y is the identity in case of matrix
            // multiplication
            if (y.p->isIdentity()) {
              e = makeIdent(start, var);
            } else {
              e = yCopy;
            }
          } else {
            e = yCopy;
          }
          computeTable.insert(xCopy, yCopy, {e.p, e.w});
          e.w = cn.mulCached(x.w, y.w);
          if (e.w.approximatelyZero()) {
            cn.returnToCache(e.w);
            return ResultEdge::zero;
          }
          return e;
        }

        if constexpr (n == NEDGE) {
          // additionally check if y is the identity in case of matrix
          // multiplication
          if (y.p->isIdentity()) {
            e = xCopy;
            computeTable.insert(xCopy, yCopy, {e.p, e.w});
            e.w = cn.mulCached(x.w, y.w);

            if (e.w.approximatelyZero()) {
              cn.returnToCache(e.w);
              return ResultEdge::zero;
            }
            return e;
          }
        }
      }
    }

    constexpr std::size_t rows = RADIX;
    constexpr std::size_t cols = n == NEDGE ? RADIX : 1U;

    std::array<ResultEdge, n> edge{};
    for (auto i = 0U; i < rows; i++) {
      for (auto j = 0U; j < cols; j++) {
        auto idx = cols * i + j;
        edge[idx] = ResultEdge::zero;
        for (auto k = 0U; k < rows; k++) {
          LEdge e1{};
          if (!x.isTerminal() && x.p->v == var) {
            e1 = x.p->e[rows * i + k];
          } else {
            e1 = xCopy;
          }

          REdge e2{};
          if (!y.isTerminal() && y.p->v == var) {
            e2 = y.p->e[j + cols * k];
          } else {
            e2 = yCopy;
          }

          if constexpr (std::is_same_v<LeftOperandNode, dNode>) {
            dEdge m;
            dEdge::applyDmChangesToEdges(e1, e2);
            if (!generateDensityMatrix || idx == 1) {
              // When generateDensityMatrix is false or I have the first edge I
              // don't optimize anything and set generateDensityMatrix to false
              // for all child edges
              m = multiply2(e1, e2, static_cast<Qubit>(var - 1), start, false);
            } else if (idx == 2) {
              // When I have the second edge and generateDensityMatrix == false,
              // then edge[2] == edge[1]
              if (k == 0) {
                if (edge[1].w.approximatelyZero()) {
                  edge[2] = ResultEdge::zero;
                } else {
                  edge[2] = {edge[1].p, cn.getCached(edge[1].w)};
                }
              }
              continue;
            } else {
              m = multiply2(e1, e2, static_cast<Qubit>(var - 1), start,
                            generateDensityMatrix);
            }

            if (k == 0 || edge[idx].w.exactlyZero()) {
              edge[idx] = m;
            } else if (!m.w.exactlyZero()) {
              dEdge::applyDmChangesToEdges(edge[idx], m);
              const auto w = edge[idx].w;
              edge[idx] = add2(edge[idx], m);
              dEdge::revertDmChangesToEdges(edge[idx], e2);
              cn.returnToCache(w);
              cn.returnToCache(m.w);
            }
            // Undo modifications on density matrices
            dEdge::revertDmChangesToEdges(e1, e2);
          } else {
            auto m = multiply2(e1, e2, static_cast<Qubit>(var - 1), start);

            if (k == 0 || edge[idx].w.exactlyZero()) {
              edge[idx] = m;
            } else if (!m.w.exactlyZero()) {
              const auto w = edge[idx].w;
              edge[idx] = add2(edge[idx], m);
              cn.returnToCache(w);
              cn.returnToCache(m.w);
            }
          }
        }
      }
    }
    e = makeDDNode(var, edge, true, generateDensityMatrix);
    computeTable.insert(xCopy, yCopy, {e.p, e.w});

    if (!e.w.exactlyZero()) {
      if (e.w.exactlyOne()) {
        e.w = cn.mulCached(x.w, y.w);
      } else {
        ComplexNumbers::mul(e.w, e.w, x.w);
        ComplexNumbers::mul(e.w, e.w, y.w);
      }
      if (e.w.approximatelyZero()) {
        cn.returnToCache(e.w);
        return ResultEdge::zero;
      }
    }
    return e;
  }

  void
  DDArrMultiplyIP2(const Edge<mNode>& x, 
      const dd::fp* y_real, const dd::fp* y_imag, 
      dd::fp* z_real, dd::fp* z_imag,
      const Qubit var, const size_t offset0, const size_t offset1, const size_t output_offset) {
    using LEdge = Edge<mNode>;
    
    // assigning 
    int n_thread = std::pow(2, n_thread_exp);
    std::vector<std::vector<LEdge>> x_vec(n_thread, std::vector<LEdge>());
    std::vector<std::vector<size_t>> yoffset_vec(n_thread, std::vector<size_t>());
    std::vector<std::vector<dd::fp>> fact_r_vec(n_thread, std::vector<dd::fp>());
    std::vector<std::vector<dd::fp>> fact_i_vec(n_thread, std::vector<dd::fp>());

    size_t seg_size = offset1 / n_thread;
    AssignParVectorIP(x, x_vec, yoffset_vec, fact_r_vec, fact_i_vec, n_thread_exp, 
      0, 0, var, 0, seg_size, 1, 0);
    std::vector<std::thread> threads;
    for (size_t i = 0; i < n_thread; i++) // parfor, task graph may apply to mitigate workload imbalance
    {
      threads.emplace_back([this, i, &x_vec, &y_real, &y_imag, &z_real, &z_imag, var, 
        &yoffset_vec, seg_size, &fact_r_vec, &fact_i_vec](){ 
        for (size_t j = 0; j < x_vec[i].size(); j++)
        {
          DDArrMultiplyRecurIP(x_vec[i][j], y_real, y_imag, z_real, z_imag, 
            var-n_thread_exp, yoffset_vec[i][2*j], yoffset_vec[i][2*j+1], seg_size * i,
            fact_r_vec[i][j], fact_i_vec[i][j]);
        }
      });
    }
    for (auto &thread : threads)
    {
      thread.join();
    }
  }

  void AssignParVectorIP(const Edge<mNode>& x, std::vector<std::vector<Edge<mNode>>> &x_vec,
    std::vector<std::vector<size_t>> &yoffset_vec,
    std::vector<std::vector<dd::fp>> &fact_r_vec,
    std::vector<std::vector<dd::fp>> &fact_i_vec,
    size_t max_lev, size_t cur_lev, size_t mat_row_off, Qubit var,
    size_t vec_row_off, size_t seg_size, dd::fp fact_r, dd::fp fact_i) {
    using LEdge = Edge<mNode>;
    if (x.w.exactlyZero()) {
      return;
    }
    if (max_lev == cur_lev) {
      x_vec[mat_row_off].push_back(x);
      yoffset_vec[mat_row_off].push_back(vec_row_off);
      yoffset_vec[mat_row_off].push_back((vec_row_off+seg_size));
      fact_r_vec[mat_row_off].push_back(fact_r);
      fact_i_vec[mat_row_off].push_back(fact_i);
      return;
    }
    else {
      fp ar = fact_r;
      fp ai = fact_i;
      fp br = RealNumber::val(x.w.r);
      fp bi = RealNumber::val(x.w.i);

      fp z1r = ar * br - ai * bi;
      fp z1i = ar * bi + ai * br;

      for (size_t i = 0; i < 2; i++)
      {
        for (size_t j = 0; j < 2; j++)
        {
          AssignParVectorIP(x.p->e[i*2+j], x_vec, yoffset_vec, fact_r_vec, fact_i_vec,
            max_lev, cur_lev+1, mat_row_off+i*std::pow(2, max_lev-cur_lev-1), var, vec_row_off+std::pow(2, var-cur_lev)*(j), seg_size, z1r, z1i);
        }
      }
    }
  }


  void
  DDArrMultiplyRecurIP(const Edge<mNode>& x, 
      const dd::fp* y_real, const dd::fp* y_imag, 
      dd::fp* z_real, dd::fp* z_imag,
      const Qubit var, const size_t offset0, const size_t offset1, const size_t output_offset, dd::fp fact_r, dd::fp fact_i) {
    using LEdge = Edge<mNode>;

    if (x.w.exactlyZero()) {
      return;
    }

    if (x.isTerminal()) {
      assert(offset1 == offset0+1);
      // MAC op
      fp ar = fact_r;
      fp ai = fact_i;
      fp br = RealNumber::val(x.w.r);
      fp bi = RealNumber::val(x.w.i);

      fp z1r = ar * br - ai * bi;
      fp z1i = ar * bi + ai * br;

      fp yr = y_real[offset0];
      fp yi = y_imag[offset0];

      fp z2r = z1r * yr - z1i * yi;
      fp z2i = z1r * yi + z1i * yr;

      z_real[output_offset] += z2r;
      z_imag[output_offset] += z2i; 

      return;
    }

    constexpr std::size_t rows = 2;
    fp ar = fact_r;
    fp ai = fact_i;
    fp br = RealNumber::val(x.w.r);
    fp bi = RealNumber::val(x.w.i);

    fp z1r = ar * br - ai * bi;
    fp z1i = ar * bi + ai * br;
    for (auto i = 0U; i < rows; i++) {
      LEdge e1{};
      assert(!x.isTerminal() && x.p->v == var);
      e1 = x.p->e[rows * i];
      size_t mid_offset = (offset0+offset1) / 2;

      DDArrMultiplyRecurIP(e1, y_real, y_imag, z_real, z_imag, static_cast<Qubit>(var - 1), offset0, mid_offset, output_offset+i*std::pow(2, var), z1r, z1i);
      LEdge e2{};
      e2 = x.p->e[rows * i+1];
      
      DDArrMultiplyRecurIP(e2, y_real, y_imag, z_real, z_imag, static_cast<Qubit>(var - 1), mid_offset, offset1, output_offset+i*std::pow(2, var), z1r, z1i);
    }
  }



  void
  DDArrMultiplyOP2(const Edge<mNode>& x, 
    const dd::fp* y_real, const dd::fp* y_imag, 
    dd::fp* z_real, dd::fp* z_imag,
            const Qubit var, const size_t offset0, const size_t offset1, const size_t output_offset,
    std::vector<dd::fp*> z_real_vec, std::vector<dd::fp*> z_imag_vec) {
    using LEdge = Edge<mNode>;
    
    // assigning 
    int n_thread = std::pow(2, n_thread_exp);
    std::vector<std::vector<LEdge>> x_vec(n_thread, std::vector<LEdge>());
    std::vector<std::vector<size_t>> yoffset_vec(n_thread, std::vector<size_t>());
    std::vector<std::vector<dd::fp>> fact_r_vec(n_thread, std::vector<dd::fp>());
    std::vector<std::vector<dd::fp>> fact_i_vec(n_thread, std::vector<dd::fp>());

    size_t seg_size = offset1 / n_thread;
    std::vector<std::thread> threads;
    
    AssignParVectorOP(x, x_vec, yoffset_vec, fact_r_vec, fact_i_vec, n_thread_exp, 
      0, 0, var, 0, seg_size, 1, 0);

    int num_buf = 0;

    std::vector<std::vector<std::pair<int, int>>> buffer;
    std::vector<int> buf_indices;
    for (size_t i = 0; i < x_vec.size(); i++)
    {
      int add_to = -1;

      for (size_t j = 0; j < buffer.size(); j++)
      {
        bool can_add = true;
        for (size_t k = 0; k < buffer[j].size(); k++) {
          if (!can_add) break;
          for (size_t t = 0; t < yoffset_vec[i].size(); t++)
          {
            if (buffer[j][k].first < yoffset_vec[i][t] + seg_size && buffer[j][k].second > yoffset_vec[i][t]) {
              can_add = false;
              break;
            }
          }
        }
        if (can_add) {
          add_to = j;
          buf_indices.push_back(add_to);
          // add
          for (size_t t = 0; t < yoffset_vec[i].size(); t++)
          {
            buffer[add_to].push_back(std::make_pair(yoffset_vec[i][t], yoffset_vec[i][t]+seg_size));
          }
          std::sort(buffer[add_to].begin(), buffer[add_to].end(), [] (const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
          });
          
          break;
        }
      }
      if (add_to == -1) {
        std::vector<std::pair<int, int>> buf_tmp;
        for (size_t j = 0; j < yoffset_vec[i].size(); j++)
        {
          buf_tmp.push_back(std::make_pair<int, int>(yoffset_vec[i][j], yoffset_vec[i][j]+seg_size));
        }
        buffer.push_back(buf_tmp);
        buf_indices.push_back(num_buf);
        num_buf++;
      }
    }
    for (int i = 0; i < num_buf; i++) {
        std::memset(z_real_vec[i], 0, sizeof(fp) * (offset1-offset0));
        std::memset(z_imag_vec[i], 0, sizeof(fp) * (offset1-offset0));
    }

    for (size_t i = 0; i < n_thread; i++) // parfor, task graph may apply to mitigate workload imbalance
    {
      threads.emplace_back([this, i, &x_vec, &y_real, &y_imag, &z_real_vec, &z_imag_vec, var, buf_indices,
        &yoffset_vec, seg_size, &fact_r_vec, &fact_i_vec, offset1, offset0]()
        { 

        DMAVTable<> dmav_table;
        for (size_t j = 0; j < x_vec[i].size(); j++)
        {
          auto tab_ret = dmav_table.lookup(x_vec[i][j].p);
          if (tab_ret.real_factor == -1 && tab_ret.imag_factor == -1) {
            DDArrMultiplyRecurIP(
              x_vec[i][j], y_real, y_imag, z_real_vec[buf_indices[i]], z_imag_vec[buf_indices[i]], 
              var-n_thread_exp, seg_size * i, seg_size * (i+1), yoffset_vec[i][2*j],
              fact_r_vec[i][j], fact_i_vec[i][j]
            );

            fp xr = RealNumber::val(x_vec[i][j].w.r);
            fp xi = RealNumber::val(x_vec[i][j].w.i);

            fp fr = xr * fact_r_vec[i][j] - xi * fact_i_vec[i][j];
            fp fi = xr * fact_i_vec[i][j] + xi * fact_r_vec[i][j];

            dmav_table.insert(x_vec[i][j].p, CachedSegment{yoffset_vec[i][2*j], yoffset_vec[i][2*j+1],
              fr, fi});
          }
          else {
            size_t z_beg = tab_ret.beg_idx;
            size_t z_end = tab_ret.end_idx;
            fp fr = tab_ret.real_factor;
            fp fi = tab_ret.imag_factor;
            fp xr = RealNumber::val(x_vec[i][j].w.r);
            fp xi = RealNumber::val(x_vec[i][j].w.i);

            fp x_fact_r = xr * fact_r_vec[i][j] - xi * fact_i_vec[i][j];
            fp x_fact_i = xr * fact_i_vec[i][j] + xi * fact_r_vec[i][j];

            fp common_r = (x_fact_r * fr + x_fact_i * fi) / (fr*fr+fi*fi);
            fp common_i = (x_fact_i * fr - x_fact_r * fi) / (fr*fr+fi*fi);

            const __m256d commonr4 = _mm256_set1_pd(common_r);
            const __m256d commoni4 = _mm256_set1_pd(common_i);

            for ( int k = yoffset_vec[i][2*j]; k < (yoffset_vec[i][2*j+1]  & ~0x3); k+= 4 ) {
              const __m256d zr4   = _mm256_loadu_pd( &(z_real_vec[buf_indices[i]][z_beg+k-yoffset_vec[i][2*j]]) );
              const __m256d zi4   = _mm256_loadu_pd( &(z_imag_vec[buf_indices[i]][z_beg+k-yoffset_vec[i][2*j]]) );
              
              __m256d zr4_m_commonr4 = _mm256_mul_pd(zr4, commonr4);
              __m256d zi4_m_commoni4 = _mm256_mul_pd(zi4, commoni4);
              __m256d zr4_m_commoni4 = _mm256_mul_pd(zr4, commoni4);
              __m256d zi4_m_commonr4 = _mm256_mul_pd(zi4, commonr4);

              __m256d resr4 = _mm256_add_pd( zr4_m_commonr4, -zi4_m_commoni4 );
              __m256d resi4 = _mm256_add_pd( zr4_m_commoni4, zi4_m_commonr4 );

              _mm256_storeu_pd( &(z_real_vec[buf_indices[i]][k]), resr4 );
              _mm256_storeu_pd( &(z_imag_vec[buf_indices[i]][k]), resi4 );
            }
          }

        }
        dmav_table.clear();
      }
      );
    }
    for (auto &thread : threads)
    {
      thread.join();
    }
    threads.clear();
    
    // merge the results, on CPU
    for (size_t i = 0; i < n_thread; i++) {
      threads.emplace_back([this, i, &z_real, &z_imag, &z_real_vec, &z_imag_vec, seg_size, num_buf](){ 
        for (size_t j = 0; j < num_buf; j++)
        {
          for(size_t k = 0 ; k < (seg_size & ~0x3); k += 4) 
          {
            const __m256d kA4   = _mm256_loadu_pd( &(z_real_vec[j][k+i*seg_size]) );
            const __m256d kB4   = _mm256_loadu_pd( &(z_real[k+i*seg_size]) );

            const __m256d kRes = _mm256_add_pd( kA4, kB4 );
            _mm256_storeu_pd( &(z_real[k+i*seg_size]), kRes );

            const __m256d kC4   = _mm256_loadu_pd( &(z_imag_vec[j][k+i*seg_size]) );
            const __m256d kD4   = _mm256_loadu_pd( &(z_imag[k+i*seg_size]) );

            const __m256d kRes1 = _mm256_add_pd( kC4, kD4 );
            _mm256_storeu_pd( &(z_imag[k+i*seg_size]), kRes1 );
          }
          std::memset(z_real_vec[j]+i*seg_size, 0, sizeof(fp) * seg_size);
          std::memset(z_imag_vec[j]+i*seg_size, 0, sizeof(fp) * seg_size);
        }
      });
    }
    for (auto &thread : threads)
    {
      thread.join();
    }
    // threads.clear();
    // for (size_t i = 0; i < num_buf; i++) {
    //   threads.emplace_back([this, i, offset0, offset1, &z_real_vec, &z_imag_vec](){ 
    //     std::memset(z_real_vec[i], 0, sizeof(fp) * (offset1-offset0));
    //     std::memset(z_imag_vec[i], 0, sizeof(fp) * (offset1-offset0));
    //   });
    // }
    // for (auto &thread : threads)
    // {
    //   thread.join();
    // }
    // fp* z_real_dev;
    // fp* z_imag_dev;
    // fp* z_real_new_dev;
    // fp* z_imag_new_dev;

    // cudaMalloc(&z_real_dev, (offset1-offset0)*sizeof(fp));
    // cudaMalloc(&z_imag_dev, (offset1-offset0)*sizeof(fp));
    // cudaMalloc(&z_real_new_dev, (offset1-offset0)*sizeof(fp));
    // cudaMalloc(&z_imag_new_dev, (offset1-offset0)*sizeof(fp));
    // cudaMemset(z_real_dev, 0, (offset1-offset0)*sizeof(fp));
    // cudaMemset(z_imag_dev, 0, (offset1-offset0)*sizeof(fp));

    // for (size_t i = 0; i < n_thread; i++) {
    //   cudaMemcpy(z_real_new_dev, z_real_vec[i], (offset1-offset0)*sizeof(fp), cudaMemcpyHostToDevice);
    //   cudaMemcpy(z_imag_new_dev, z_imag_vec[i], (offset1-offset0)*sizeof(fp), cudaMemcpyHostToDevice);

    //   call_kernel(offset1-offset0, z_real_dev, z_real_new_dev, z_imag_dev, z_imag_new_dev);

    // }
    // cudaMemcpy(z_real, z_real_dev, (offset1-offset0)*sizeof(fp), cudaMemcpyDeviceToHost);
    // cudaMemcpy(z_imag, z_imag_dev, (offset1-offset0)*sizeof(fp), cudaMemcpyDeviceToHost);

  }

  void AssignParVectorOP(const Edge<mNode>& x, std::vector<std::vector<Edge<mNode>>> &x_vec,
    std::vector<std::vector<size_t>> &yoffset_vec,
    std::vector<std::vector<dd::fp>> &fact_r_vec,
    std::vector<std::vector<dd::fp>> &fact_i_vec,
    size_t max_lev, size_t cur_lev, size_t mat_col_off, Qubit var,
    size_t vec_row_off, size_t seg_size, dd::fp fact_r, dd::fp fact_i) {
    using LEdge = Edge<mNode>;
    if (x.w.exactlyZero()) {
      return;
    }
    if (max_lev == cur_lev) {
      x_vec[mat_col_off].push_back(x);
      yoffset_vec[mat_col_off].push_back(vec_row_off);
      yoffset_vec[mat_col_off].push_back((vec_row_off+seg_size));
      fact_r_vec[mat_col_off].push_back(fact_r);
      fact_i_vec[mat_col_off].push_back(fact_i);
      return;
    }
    else {
      fp ar = fact_r;
      fp ai = fact_i;
      fp br = RealNumber::val(x.w.r);
      fp bi = RealNumber::val(x.w.i);

      fp z1r = ar * br - ai * bi;
      fp z1i = ar * bi + ai * br;

      for (size_t i = 0; i < 2; i++)
      {
        for (size_t j = 0; j < 2; j++)
        {
          AssignParVectorOP(x.p->e[j*2+i], x_vec, yoffset_vec, fact_r_vec, fact_i_vec,
            max_lev, cur_lev+1, mat_col_off+i*std::pow(2, max_lev-cur_lev-1), var, vec_row_off+std::pow(2, var-cur_lev)*(j), seg_size, z1r, z1i);
        }
      }
    }
  }

  ///
  /// Inner product, fidelity, expectation value
  ///
public:
  ComputeTable<vEdge, vEdge, vCachedEdge, Config::CT_VEC_INNER_PROD_NBUCKET>
      vectorInnerProduct{};

  /**
      Calculates the inner product of two vector decision diagrams x and y.
      @param x a vector DD representing a quantum state
      @param y a vector DD representing a quantum state
      @return a complex number representing the scalar product of the DDs
  **/
  ComplexValue innerProduct(const vEdge& x, const vEdge& y) {
    if (x.isTerminal() || y.isTerminal() || x.w.approximatelyZero() ||
        y.w.approximatelyZero()) { // the 0 case
      return {0, 0};
    }

    [[maybe_unused]] const auto before = cn.cacheCount();

    auto w = x.p->v;
    if (y.p->v > w) {
      w = y.p->v;
    }
    // Overall normalization factor needs to be conjugated
    // before input into recursive private function
    auto xCopy = vEdge{x.p, ComplexNumbers::conj(x.w)};
    const ComplexValue ip = innerProduct(xCopy, y, w + 1U);

    [[maybe_unused]] const auto after = cn.cacheCount();
    assert(after == before);

    return ip;
  }

  fp fidelity(const vEdge& x, const vEdge& y) {
    const auto fid = innerProduct(x, y);
    return fid.r * fid.r + fid.i * fid.i;
  }

  dd::fp fidelityOfMeasurementOutcomes(const vEdge& e,
                                       const ProbabilityVector& probs) {
    if (e.w.approximatelyZero()) {
      return 0.;
    }
    return fidelityOfMeasurementOutcomesRecursive(e, probs, 0);
  }

  dd::fp fidelityOfMeasurementOutcomesRecursive(const vEdge& e,
                                                const ProbabilityVector& probs,
                                                const std::size_t i) {
    const auto topw = dd::ComplexNumbers::mag(e.w);
    if (e.isTerminal()) {
      if (auto it = probs.find(i); it != probs.end()) {
        return topw * std::sqrt(it->second);
      }
      return 0.;
    }

    std::size_t leftIdx = i;
    dd::fp leftContribution = 0.;
    if (!e.p->e[0].w.approximatelyZero()) {
      leftContribution =
          fidelityOfMeasurementOutcomesRecursive(e.p->e[0], probs, leftIdx);
    }

    std::size_t rightIdx = i | (1ULL << e.p->v);
    auto rightContribution = 0.;
    if (!e.p->e[1].w.approximatelyZero()) {
      rightContribution =
          fidelityOfMeasurementOutcomesRecursive(e.p->e[1], probs, rightIdx);
    }

    return topw * (leftContribution + rightContribution);
  }

private:
  /**
      Private function to recursively calculate the inner product of two vector
  decision diagrams x and y with var levels.
      @param x a vector DD representing a quantum state
      @param y a vector DD representing a quantum state
      @param var the number of levels contained in each vector DD
      @return a complex number  representing the scalar product of the DDs
      @note This function is called recursively such that the number of levels
  decreases each time to traverse the DDs.
  **/
  ComplexValue innerProduct(const vEdge& x, const vEdge& y, Qubit var) {
    if (x.w.approximatelyZero() || y.w.approximatelyZero()) { // the 0 case
      return {0.0, 0.0};
    }

    if (var == 0) { // Multiplies terminal weights
      auto c = cn.mulTemp(x.w, y.w);
      return {c.r->value, c.i->value};
    }

    // Set to one to generate more lookup hits
    auto xCopy = vEdge{x.p, Complex::one};
    auto yCopy = vEdge{y.p, Complex::one};
    if (const auto* r = vectorInnerProduct.lookup(xCopy, yCopy); r != nullptr) {
      auto c = cn.getTemporary(r->w);
      ComplexNumbers::mul(c, c, x.w);
      ComplexNumbers::mul(c, c, y.w);
      return {c.r->value, c.i->value};
    }

    auto w = static_cast<Qubit>(var - 1U);
    ComplexValue sum{0.0, 0.0};
    // Iterates through edge weights recursively until terminal
    for (auto i = 0U; i < RADIX; i++) {
      vEdge e1{};
      if (!x.isTerminal() && x.p->v == w) {
        e1 = x.p->e[i];
        e1.w = ComplexNumbers::conj(e1.w);
      } else {
        e1 = xCopy;
      }
      vEdge e2{};
      if (!y.isTerminal() && y.p->v == w) {
        e2 = y.p->e[i];
      } else {
        e2 = yCopy;
      }
      auto cv = innerProduct(e1, e2, w);
      sum.r += cv.r;
      sum.i += cv.i;
    }

    vectorInnerProduct.insert(xCopy, yCopy, {vNode::getTerminal(), sum});
    auto c = cn.getTemporary(sum);
    ComplexNumbers::mul(c, c, x.w);
    ComplexNumbers::mul(c, c, y.w);
    return {c.r->value, c.i->value};
  }

public:
  /**
      Calculates the expectation value of an operator x with respect to a
  quantum state y given their corresponding decision diagrams.
      @param x a matrix DD representing the operator
      @param y a vector DD representing the quantum state
      @return a floating point value representing the expectation value of the
  operator with respect to the quantum state
      @throw an exception message is thrown if the edges are not on the same
  level or if the expectation value is non-real.
      @note This function calls the multiply() function to apply the operator to
  the quantum state, then calls innerProduct() to calculate the overlap between
  the original state and the applied state i.e. <Psi| Psi'> = <Psi| (Op|Psi>).
            It also calls the garbageCollect() function to free up any unused
  memory.
  **/
  fp expectationValue(const mEdge& x, const vEdge& y) {
    assert(!x.isTerminal() && !y.isTerminal());
    if (x.p->v != y.p->v) {
      throw std::invalid_argument(
          "Observable and state must act on the same number of qubits to "
          "compute the expectation value.");
    }

    auto yPrime = multiply(x, y);
    const ComplexValue expValue = innerProduct(y, yPrime);

    assert(RealNumber::approximatelyZero(expValue.i));

    garbageCollect();

    return expValue.r;
  }

  ///
  /// Kronecker/tensor product
  ///

  ComputeTable<vEdge, vEdge, vCachedEdge, Config::CT_VEC_KRON_NBUCKET>
      vectorKronecker{};
  ComputeTable<mEdge, mEdge, mCachedEdge, Config::CT_MAT_KRON_NBUCKET>
      matrixKronecker{};

  template <class Node> [[nodiscard]] auto& getKroneckerComputeTable() {
    if constexpr (std::is_same_v<Node, vNode>) {
      return vectorKronecker;
    } else {
      return matrixKronecker;
    }
  }

  template <class Edge>
  Edge kronecker(const Edge& x, const Edge& y, const bool incIdx = true) {
    if constexpr (std::is_same_v<Edge, dEdge>) {
      throw std::invalid_argument(
          "Kronecker is currently not supported for density matrices");
    }

    auto e = kronecker2(x, y, incIdx);
    e.w = cn.lookup(e.w, true);

    return e;
  }

  // extent the DD pointed to by `e` with `h` identities on top and `l`
  // identities at the bottom
  mEdge extend(const mEdge& e, const Qubit h, const Qubit l = 0) {
    auto f = (l > 0) ? kronecker(e, makeIdent(l)) : e;
    auto g = (h > 0) ? kronecker(makeIdent(h), f) : f;
    return g;
  }

private:
  template <class Node>
  Edge<Node> kronecker2(const Edge<Node>& x, const Edge<Node>& y,
                        const bool incIdx = true) {
    if (x.w.approximatelyZero() || y.w.approximatelyZero()) {
      return Edge<Node>::zero;
    }

    if (x.isTerminal()) {
      return {y.p, cn.mulCached(x.w, y.w)};
    }

    auto& computeTable = getKroneckerComputeTable<Node>();
    if (const auto* r = computeTable.lookup(x, y); r != nullptr) {
      if (r->w.approximatelyZero()) {
        return Edge<Node>::zero;
      }
      return {r->p, cn.getCached(r->w)};
    }

    constexpr std::size_t n = std::tuple_size_v<decltype(x.p->e)>;
    // special case handling for matrices
    if constexpr (n == NEDGE) {
      if (x.p->isIdentity()) {
        auto idx = incIdx ? static_cast<Qubit>(y.p->v + 1) : y.p->v;
        auto e = makeDDNode(
            idx, std::array{y, Edge<Node>::zero, Edge<Node>::zero, y});
        for (auto i = 0; i < x.p->v; ++i) {
          idx = incIdx ? (e.p->v + 1) : e.p->v;
          e = makeDDNode(idx,
                         std::array{e, Edge<Node>::zero, Edge<Node>::zero, e});
        }

        e.w = cn.getCached(y.w);
        computeTable.insert(x, y, {e.p, e.w});
        return e;
      }
    }

    std::array<Edge<Node>, n> edge{};
    for (auto i = 0U; i < n; ++i) {
      edge[i] = kronecker2(x.p->e[i], y, incIdx);
    }

    auto idx = incIdx ? (y.p->v + x.p->v + 1) : x.p->v;
    auto e = makeDDNode(static_cast<Qubit>(idx), edge, true);
    ComplexNumbers::mul(e.w, e.w, x.w);
    computeTable.insert(x, y, {e.p, e.w});
    return e;
  }

  ///
  /// (Partial) trace
  ///
public:
  mEdge partialTrace(const mEdge& a, const std::vector<bool>& eliminate) {
    [[maybe_unused]] const auto before = cn.cacheCount();
    const auto result = trace(a, eliminate);
    [[maybe_unused]] const auto after = cn.cacheCount();
    assert(before == after);
    return result;
  }
  ComplexValue trace(const mEdge& a) {
    const auto eliminate = std::vector<bool>(nqubits, true);
    [[maybe_unused]] const auto before = cn.cacheCount();
    const auto res = partialTrace(a, eliminate);
    [[maybe_unused]] const auto after = cn.cacheCount();
    assert(before == after);
    return {RealNumber::val(res.w.r), RealNumber::val(res.w.i)};
  }
  bool isCloseToIdentity(const mEdge& m, const dd::fp tol = 1e-10) {
    std::unordered_set<decltype(m.p)> visited{};
    visited.reserve(mUniqueTable.getStats().activeEntryCount);
    return isCloseToIdentityRecursive(m, visited, tol);
  }

private:
  /// TODO: introduce a compute table for the trace?
  mEdge trace(const mEdge& a, const std::vector<bool>& eliminate,
              std::size_t alreadyEliminated = 0) {
    if (a.w.approximatelyZero()) {
      return mEdge::zero;
    }

    if (a.isTerminal() || std::none_of(eliminate.begin(), eliminate.end(),
                                       [](bool v) { return v; })) {
      return a;
    }

    const auto v = a.p->v;
    if (eliminate[v]) {
      const auto elims = alreadyEliminated + 1;
      auto r = mEdge::zero;

      const auto t0 = trace(a.p->e[0], eliminate, elims);
      r = add2(r, t0);
      auto r1 = r;

      const auto t1 = trace(a.p->e[3], eliminate, elims);
      r = add2(r, t1);
      auto r2 = r;

      if (r.w.exactlyOne()) {
        r.w = a.w;
      } else {
        // better safe than sorry. this may result in complex
        // values with magnitude > 1 in the complex table
        r.w = cn.lookup(cn.mulTemp(r.w, a.w));
      }

      cn.returnToCache(r1.w);
      cn.returnToCache(r2.w);

      return r;
    }

    std::array<mEdge, NEDGE> edge{};
    std::transform(
        a.p->e.cbegin(), a.p->e.cend(), edge.begin(),
        [this, &eliminate, &alreadyEliminated](const mEdge& e) -> mEdge {
          return trace(e, eliminate, alreadyEliminated);
        });
    const auto adjustedV =
        static_cast<Qubit>(static_cast<std::size_t>(a.p->v) -
                           (static_cast<std::size_t>(std::count(
                                eliminate.begin(), eliminate.end(), true)) -
                            alreadyEliminated));
    auto r = makeDDNode(adjustedV, edge);
    if (r.w.exactlyOne()) {
      r.w = a.w;
    } else {
      r.w = cn.lookup(cn.mulTemp(r.w, a.w));
    }
    return r;
  }

  bool isCloseToIdentityRecursive(const mEdge& m,
                                  std::unordered_set<decltype(m.p)>& visited,
                                  const dd::fp tol) {
    // immediately return if this node has already been visited
    if (visited.find(m.p) != visited.end()) {
      return true;
    }

    // immediately return of this node is identical to the identity
    if (m.isTerminal() || m.p->isIdentity()) {
      return true;
    }

    // check whether any of the middle successors is non-zero, i.e., m = [ x 0 0
    // y ]
    const auto mag1 = dd::ComplexNumbers::mag2(m.p->e[1U].w);
    const auto mag2 = dd::ComplexNumbers::mag2(m.p->e[2U].w);
    if (mag1 > tol || mag2 > tol) {
      visited.insert(m.p);
      return false;
    }

    // check whether  m = [ ~1 0 0 y ]
    const auto mag0 = dd::ComplexNumbers::mag2(m.p->e[0U].w);
    if (std::abs(mag0 - 1.0) > tol) {
      visited.insert(m.p);
      return false;
    }
    const auto arg0 = dd::ComplexNumbers::arg(m.p->e[0U].w);
    if (std::abs(arg0) > tol) {
      visited.insert(m.p);
      return false;
    }

    // check whether m = [ x 0 0 ~1 ] or m = [ x 0 0 ~0 ] (the last case is true
    // for an ancillary qubit)
    const auto mag3 = dd::ComplexNumbers::mag2(m.p->e[3U].w);
    if (mag3 > tol) {
      if (std::abs(mag3 - 1.0) > tol) {
        visited.insert(m.p);
        return false;
      }
      const auto arg3 = dd::ComplexNumbers::arg(m.p->e[3U].w);
      if (std::abs(arg3) > tol) {
        visited.insert(m.p);
        return false;
      }
    }

    // m either has the form [ ~1 0 0 ~1 ] or [ ~1 0 0 ~0 ]
    const auto ident0 = isCloseToIdentityRecursive(m.p->e[0U], visited, tol);
    if (!ident0) {
      visited.insert(m.p);
      return false;
    }

    // m either has the form [ I 0 0 ~1 ] or [ I 0 0 ~0 ]
    const auto ident3 = isCloseToIdentityRecursive(m.p->e[3U], visited, tol);
    visited.insert(m.p);
    return ident3;
  }

public:
  ///
  /// Identity matrices
  ///
  // create n-qubit identity DD. makeIdent(n) === makeIdent(0, n-1)
  mEdge makeIdent(const std::size_t n) {
    if (n == 0U) {
      return mEdge::one;
    }
    return makeIdent(0, n - 1);
  }
  mEdge makeIdent(const std::size_t leastSignificantQubit,
                  const std::size_t mostSignificantQubit) {
    if (mostSignificantQubit < leastSignificantQubit) {
      return mEdge::one;
    }

    const auto& entry = idTable.at(mostSignificantQubit);
    if (leastSignificantQubit == 0 && entry.p != nullptr) {
      return entry;
    }

    if (mostSignificantQubit >= 1) {
      const auto& prevEntry = idTable.at(mostSignificantQubit - 1);
      if (!prevEntry.isTerminal()) {
        idTable.at(mostSignificantQubit) = makeDDNode(
            static_cast<Qubit>(mostSignificantQubit),
            std::array{prevEntry, mEdge::zero, mEdge::zero, prevEntry});
        return idTable[mostSignificantQubit];
      }
    }

    auto e = makeDDNode(
        static_cast<Qubit>(leastSignificantQubit),
        std::array{mEdge::one, mEdge::zero, mEdge::zero, mEdge::one});
    for (auto k = leastSignificantQubit + 1; k <= mostSignificantQubit; ++k) {
      e = makeDDNode(static_cast<Qubit>(k),
                     std::array{e, mEdge::zero, mEdge::zero, e});
    }
    if (leastSignificantQubit == 0) {
      idTable.at(mostSignificantQubit) = e;
    }
    return e;
  }

  // identity table access and reset
  [[nodiscard]] const auto& getIdentityTable() const { return idTable; }

  void clearIdentityTable() {
    for (auto& entry : idTable) {
      entry.p = nullptr;
    }
  }

  mEdge createInitialMatrix(const std::size_t n,
                            const std::vector<bool>& ancillary) {
    auto e = makeIdent(n);
    incRef(e);
    return reduceAncillae(e, ancillary);
  }

private:
  std::vector<mEdge> idTable{};

  ///
  /// Noise Operations
  ///
public:
  StochasticNoiseOperationTable<mEdge, Config::STOCHASTIC_CACHE_OPS>
      stochasticNoiseOperationCache{nqubits};
  DensityNoiseTable<dEdge, dEdge, Config::CT_DM_NOISE_NBUCKET> densityNoise{};

  ///
  /// Decision diagram size
  ///
  template <class Edge> std::size_t size(const Edge& e) {
    static constexpr std::size_t NODECOUNT_BUCKETS = 200000U;
    static std::unordered_set<decltype(e.p)> visited{NODECOUNT_BUCKETS}; // 2e6
    visited.max_load_factor(10);
    visited.clear();
    return nodeCount(e, visited);
  }

  // std::size_t DMAVMACStatOP(const mEdge &e, std::unordered_map<mNode*, std::size_t> &mac_map, std::size_t nDim, 
  //   unsigned int n_thread_exp) {
  //   std::size_t mac_cnt = DMAVMACCountOP(e, mac_map, 0, nDim, n_thread_exp);
  //   return mac_cnt;
  // }

  std::size_t DMAVMACStatOP1(const mEdge &e, std::unordered_map<mNode*, std::size_t> &mac_map, std::size_t nDim, 
    unsigned int n_thread_exp) {
    std::size_t mac_cnt = DMAVMACCountOP1(e, mac_map, 0, nDim, n_thread_exp);
    return mac_cnt;
  }

  std::size_t DMAVMACStatIP(const mEdge &e, std::unordered_map<mNode*, std::size_t> &mac_map, std::size_t nDim,
    unsigned int n_thread_exp) {
    std::size_t mac_cnt = DMAVMACCountIP(e, mac_map, 0, nDim);
    int n_threads = (int)std::pow(2, n_thread_exp);
    return mac_cnt / n_threads;
  }

  // void DMAVthreadLoadIP(const mEdge &e, std::unordered_map<mNode*, std::size_t> &mac_map, std::size_t nDim,
  //   unsigned int n_thread_exp) {
  //   int n_thread = std::pow(2, n_thread_exp);
  //   std::vector<std::vector<mEdge>> x_vec(n_thread, std::vector<mEdge>());
  //   std::vector<std::vector<size_t>> yoffset_vec(n_thread, std::vector<size_t>());
  //   std::vector<std::vector<dd::fp>> fact_r_vec(n_thread, std::vector<dd::fp>());
  //   std::vector<std::vector<dd::fp>> fact_i_vec(n_thread, std::vector<dd::fp>());

  //   size_t seg_size = nDim / n_thread;
  
    
  //   AssignParVectorIP(e, x_vec, yoffset_vec, fact_r_vec, fact_i_vec, n_thread_exp, 
  //     0, 0, e.p->v, 0, seg_size, 1, 0);
    
  //   std::vector<size_t> mac_cnt_vec;
    
  //   for (size_t i = 0; i < n_thread; i++) {
  //     size_t cnt = 0;
  //     for (size_t j = 0; j < x_vec[i].size(); j++) {
  //       mEdge thisEdge = x_vec[i][j];

  //       cnt += mac_map[thisEdge.p] * n_thread;
  //     }
  //     mac_cnt_vec.push_back(cnt);
  //   }


  //   size_t sum = std::accumulate(mac_cnt_vec.begin(), mac_cnt_vec.end(), 0.0);
  //   size_t mean = sum / mac_cnt_vec.size();

  //   size_t sq_sum = std::inner_product(mac_cnt_vec.begin(), mac_cnt_vec.end(), mac_cnt_vec.begin(), 0.0);
  //   size_t stdev = std::sqrt(sq_sum / mac_cnt_vec.size() - mean * mean);
  //   if (stdev != 0) {
  //     std::cout<<stdev<<std::endl;
  //   }
  // }

  std::size_t DMAVweightMulStatIP(const mEdge &e, std::size_t nDim,
    unsigned int n_thread_exp) {
    int n_threads = (int)std::pow(2, n_thread_exp);

    std::unordered_map<mNode*, std::size_t> wm_map;
    std::deque<mNode*> nodeQ;
    wm_map[e.p] = 1;
    nodeQ.push_back(e.p);

    while (nodeQ.size())
    {
      auto& cur_node = nodeQ.front();
      nodeQ.pop_front();
      if (cur_node != nullptr) {
        for (int i = 0; i < 2; i++)
        {
          for (int j = 0; j < 2; j++)
          {
            auto nxt_node = cur_node->e[i*2+j].p;
            auto nxt_edge = cur_node->e[i*2+j];
            if (!nxt_edge.w.exactlyZero()) {
              if (wm_map.find(nxt_node) != wm_map.end()) {
                wm_map[nxt_node] += wm_map[cur_node];
              }
              else {
                wm_map[nxt_node] = wm_map[cur_node];
                nodeQ.push_back(nxt_node);
              }
            }
          }
        }
      }
    }
    std::size_t result = 0;
    for (auto MapItem : wm_map) {
      result += MapItem.second;
    }
    return result / n_threads;
  }
  std::size_t DMAVweightMulStatOP(const mEdge &e, std::size_t nDim, 
    unsigned int n_thread_exp) {
    int n_thread = std::pow(2, n_thread_exp);
    std::vector<std::vector<mEdge>> x_vec(n_thread, std::vector<mEdge>());
    std::vector<std::vector<size_t>> yoffset_vec(n_thread, std::vector<size_t>());
    std::vector<std::vector<dd::fp>> fact_r_vec(n_thread, std::vector<dd::fp>());
    std::vector<std::vector<dd::fp>> fact_i_vec(n_thread, std::vector<dd::fp>());

    size_t seg_size = nDim / n_thread;
  
    
    AssignParVectorOP(e, x_vec, yoffset_vec, fact_r_vec, fact_i_vec, n_thread_exp, 
      0, 0, e.p->v, 0, seg_size, 1, 0);
    size_t cnt = 0;
    for (size_t i = 0; i < n_thread; i++) {
      std::set<mNode*> visited;
      for (size_t j = 0; j < x_vec[i].size(); j++) {
        mEdge thisEdge = x_vec[i][j];
        if (visited.find(thisEdge.p) == visited.end()) {
          std::unordered_map<mNode*, std::size_t> wm_map;
          std::deque<mNode*> nodeQ;
          wm_map[thisEdge.p] = 1;
          nodeQ.push_back(thisEdge.p);
          while (nodeQ.size())
          {
            auto& cur_node = nodeQ.front();
            nodeQ.pop_front();
            if (cur_node != nullptr) {
              for (int i = 0; i < 2; i++)
              {
                for (int j = 0; j < 2; j++)
                {
                  auto nxt_node = cur_node->e[i*2+j].p;
                  auto nxt_edge = cur_node->e[i*2+j];
                  if (!nxt_edge.w.exactlyZero()) {
                    if (wm_map.find(nxt_node) != wm_map.end()) {
                      wm_map[nxt_node] += wm_map[cur_node];
                    }
                    else {
                      wm_map[nxt_node] = wm_map[cur_node];
                      nodeQ.push_back(nxt_node);
                    }
                  }
                }
              }
            }
          }
          std::size_t result = 0;
          for (auto MapItem : wm_map) {
            result += MapItem.second;
          }


          visited.insert(thisEdge.p);
          cnt += result;
        }
      }
    }
    return cnt / n_thread;
  }
private:
  template <class Edge>
  std::size_t nodeCount(const Edge& e,
                        std::unordered_set<decltype(e.p)>& v) const {
    v.insert(e.p);
    std::size_t sum = 1U;
    if (!e.isTerminal()) {
      for (const auto& edge : e.p->e) {
        if (!v.count(edge.p)) {
          sum += nodeCount(edge, v);
        }
      }
    }
    return sum;
  }

  // std::size_t DMAVMACCountOP(const mEdge& e, 
  //   std::unordered_map<mNode*, std::size_t> &mac_map,
  //   std::size_t offset0, std::size_t offset1, unsigned int n_thread_exp
  // ) {
  //   int n_thread = std::pow(2, n_thread_exp);
  //   std::vector<std::vector<mEdge>> x_vec(n_thread, std::vector<mEdge>());
  //   std::vector<std::vector<size_t>> yoffset_vec(n_thread, std::vector<size_t>());
  //   std::vector<std::vector<dd::fp>> fact_r_vec(n_thread, std::vector<dd::fp>());
  //   std::vector<std::vector<dd::fp>> fact_i_vec(n_thread, std::vector<dd::fp>());

  //   size_t seg_size = offset1 / n_thread;
  
    
  //   AssignParVectorOP(e, x_vec, yoffset_vec, fact_r_vec, fact_i_vec, n_thread_exp, 
  //     0, 0, e.p->v, 0, seg_size, 1, 0);
  //   size_t cnt = 0;
  //   for (size_t i = 0; i < n_thread; i++) {
  //     std::set<mNode*> visited;
  //     for (size_t j = 0; j < x_vec[i].size(); j++) {
  //       mEdge thisEdge = x_vec[i][j];
  //       if (visited.find(thisEdge.p) != visited.end()) {
  //         cnt += seg_size / 4;
  //       } else {
  //         visited.insert(thisEdge.p);
  //         cnt += mac_map[thisEdge.p];
  //       }
  //     }
  //   }
  //   return cnt / n_thread+ offset1 / 4; //  
  // }

  std::size_t DMAVMACCountOP1(const mEdge& e, 
    std::unordered_map<mNode*, std::size_t> &mac_map,
    std::size_t offset0, std::size_t offset1, unsigned int n_thread_exp
  ) {
    int n_thread = std::pow(2, n_thread_exp);
    std::vector<std::vector<mEdge>> x_vec(n_thread, std::vector<mEdge>());
    std::vector<std::vector<size_t>> yoffset_vec(n_thread, std::vector<size_t>());
    std::vector<std::vector<dd::fp>> fact_r_vec(n_thread, std::vector<dd::fp>());
    std::vector<std::vector<dd::fp>> fact_i_vec(n_thread, std::vector<dd::fp>());

    size_t seg_size = offset1 / n_thread;
  
    
    AssignParVectorOP(e, x_vec, yoffset_vec, fact_r_vec, fact_i_vec, n_thread_exp, 
      0, 0, e.p->v, 0, seg_size, 1, 0);

    int num_buf = 0;

    std::vector<std::vector<std::pair<int, int>>> buffer;
    std::vector<int> buf_indices;
    for (size_t i = 0; i < x_vec.size(); i++)
    {
      int add_to = -1;

      for (size_t j = 0; j < buffer.size(); j++)
      {
        bool can_add = true;
        for (size_t k = 0; k < buffer[j].size(); k++) {
          if (!can_add) break;
          for (size_t t = 0; t < yoffset_vec[i].size(); t++)
          {
            if (buffer[j][k].first < yoffset_vec[i][t] + seg_size && buffer[j][k].second > yoffset_vec[i][t]) {
              can_add = false;
              break;
            }
          }
        }
        if (can_add) {
          add_to = j;
          buf_indices.push_back(add_to);
          // add
          for (size_t t = 0; t < yoffset_vec[i].size(); t++)
          {
            buffer[add_to].push_back(std::make_pair(yoffset_vec[i][t], yoffset_vec[i][t]+seg_size));
          }
          std::sort(buffer[add_to].begin(), buffer[add_to].end(), [] (const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
          });
          
          break;
        }
      }
      if (add_to == -1) {
        std::vector<std::pair<int, int>> buf_tmp;
        for (size_t j = 0; j < yoffset_vec[i].size(); j++)
        {
          buf_tmp.push_back(std::make_pair<int, int>(yoffset_vec[i][j], yoffset_vec[i][j]+seg_size));
        }
        buffer.push_back(buf_tmp);
        buf_indices.push_back(num_buf);
        num_buf++;
      }
    }
    
    
    
    size_t cnt = 0;
    for (size_t i = 0; i < n_thread; i++) {
      std::set<mNode*> visited;
      for (size_t j = 0; j < x_vec[i].size(); j++) {
        mEdge thisEdge = x_vec[i][j];
        DMAVMACCountIP(thisEdge, mac_map, 0, offset1);
        if (visited.find(thisEdge.p) != visited.end()) {
          cnt += seg_size / 4;
        } else {
          visited.insert(thisEdge.p);
          cnt += mac_map[thisEdge.p];
        }
      }
    }
    return cnt / n_thread+ offset1 * num_buf / (4 * n_thread);
  }

  std::size_t DMAVMACCountIP(const mEdge& e, 
    std::unordered_map<mNode*, std::size_t> &mac_map, 
    std::size_t offset0, std::size_t offset1
  ) {
    if (e.isTerminal()) {
      return 1;
    }
    
    if (mac_map.find(e.p) == mac_map.end()) {
      std::size_t cnt = 0;

      for (int i = 0; i < 2; i++) {
        std::size_t mid_offset = (offset0+offset1) / 2;
        if (!(e.p->e[i*2].w.exactlyZero())) {
          cnt += DMAVMACCountIP(e.p->e[i*2], mac_map, offset0, mid_offset);
        }
        if (!(e.p->e[i*2+1].w.exactlyZero())) {
          cnt += DMAVMACCountIP(e.p->e[i*2+1], mac_map, mid_offset, offset1);
        }
      }
      mac_map.insert({ e.p, cnt });
      return cnt;
    }
    else {
      return mac_map[e.p];
    }
  }

  ///
  /// Ancillary and garbage reduction
  ///
public:
  mEdge reduceAncillae(mEdge& e, const std::vector<bool>& ancillary,
                       const bool regular = true) {
    // return if no more garbage left
    if (std::none_of(ancillary.begin(), ancillary.end(),
                     [](bool v) { return v; }) ||
        e.isTerminal()) {
      return e;
    }
    Qubit lowerbound = 0;
    for (auto i = 0U; i < ancillary.size(); ++i) {
      if (ancillary[i]) {
        lowerbound = static_cast<Qubit>(i);
        break;
      }
    }
    if (e.p->v < lowerbound) {
      return e;
    }
    auto f = reduceAncillaeRecursion(e, ancillary, lowerbound, regular);
    incRef(f);
    decRef(e);
    return f;
  }

  // Garbage reduction works for reversible circuits --- to be thoroughly tested
  // for quantum circuits
  vEdge reduceGarbage(vEdge& e, const std::vector<bool>& garbage) {
    // return if no more garbage left
    if (std::none_of(garbage.begin(), garbage.end(),
                     [](bool v) { return v; }) ||
        e.isTerminal()) {
      return e;
    }
    Qubit lowerbound = 0;
    for (std::size_t i = 0U; i < garbage.size(); ++i) {
      if (garbage[i]) {
        lowerbound = static_cast<Qubit>(i);
        break;
      }
    }
    if (e.p->v < lowerbound) {
      return e;
    }
    auto f = reduceGarbageRecursion(e, garbage, lowerbound);
    incRef(f);
    decRef(e);
    return f;
  }
  mEdge reduceGarbage(mEdge& e, const std::vector<bool>& garbage,
                      const bool regular = true) {
    // return if no more garbage left
    if (std::none_of(garbage.begin(), garbage.end(),
                     [](bool v) { return v; }) ||
        e.isTerminal()) {
      return e;
    }
    Qubit lowerbound = 0;
    for (auto i = 0U; i < garbage.size(); ++i) {
      if (garbage[i]) {
        lowerbound = static_cast<Qubit>(i);
        break;
      }
    }
    if (e.p->v < lowerbound) {
      return e;
    }
    auto f = reduceGarbageRecursion(e, garbage, lowerbound, regular);
    incRef(f);
    decRef(e);
    return f;
  }

private:
  mEdge reduceAncillaeRecursion(mEdge& e, const std::vector<bool>& ancillary,
                                const Qubit lowerbound,
                                const bool regular = true) {
    if (e.p->v < lowerbound) {
      return e;
    }

    auto f = e;

    std::array<mEdge, NEDGE> edges{};
    std::bitset<NEDGE> handled{};
    for (auto i = 0U; i < NEDGE; ++i) {
      if (!handled.test(i)) {
        if (e.p->e[i].isTerminal()) {
          edges[i] = e.p->e[i];
        } else {
          edges[i] = reduceAncillaeRecursion(f.p->e[i], ancillary, lowerbound,
                                             regular);
          for (auto j = i + 1; j < NEDGE; ++j) {
            if (e.p->e[i].p == e.p->e[j].p) {
              edges[j] = edges[i];
              handled.set(j);
            }
          }
        }
        handled.set(i);
      }
    }
    f = makeDDNode(f.p->v, edges);

    // something to reduce for this qubit
    if (ancillary[f.p->v]) {
      if (regular) {
        if (f.p->e[1].w != Complex::zero || f.p->e[3].w != Complex::zero) {
          f = makeDDNode(f.p->v, std::array{f.p->e[0], mEdge::zero, f.p->e[2],
                                            mEdge::zero});
        }
      } else {
        if (f.p->e[2].w != Complex::zero || f.p->e[3].w != Complex::zero) {
          f = makeDDNode(f.p->v, std::array{f.p->e[0], f.p->e[1], mEdge::zero,
                                            mEdge::zero});
        }
      }
    }
    f.w = cn.lookup(cn.mulTemp(f.w, e.w));
    return f;
  }

  vEdge reduceGarbageRecursion(vEdge& e, const std::vector<bool>& garbage,
                               const Qubit lowerbound) {
    if (e.p->v < lowerbound) {
      return e;
    }

    auto f = e;

    std::array<vEdge, RADIX> edges{};
    std::bitset<RADIX> handled{};
    for (auto i = 0U; i < RADIX; ++i) {
      if (!handled.test(i)) {
        if (e.p->e[i].isTerminal()) {
          edges[i] = e.p->e[i];
        } else {
          edges[i] = reduceGarbageRecursion(f.p->e[i], garbage, lowerbound);
          for (auto j = i + 1; j < RADIX; ++j) {
            if (e.p->e[i].p == e.p->e[j].p) {
              edges[j] = edges[i];
              handled.set(j);
            }
          }
        }
        handled.set(i);
      }
    }
    f = makeDDNode(f.p->v, edges);

    // something to reduce for this qubit
    if (garbage[f.p->v]) {
      if (f.p->e[1].w != Complex::zero) {
        vEdge g{};
        if (f.p->e[0].w == Complex::zero && f.p->e[1].w != Complex::zero) {
          g = f.p->e[1];
        } else if (f.p->e[1].w != Complex::zero) {
          g = add(f.p->e[0], f.p->e[1]);
        } else {
          g = f.p->e[0];
        }
        f = makeDDNode(e.p->v, std::array{g, vEdge::zero});
      }
    }
    f.w = cn.lookup(cn.mulTemp(f.w, e.w));

    // Quick-fix for normalization bug
    if (ComplexNumbers::mag2(f.w) > 1.0) {
      f.w = Complex::one;
    }

    return f;
  }
  mEdge reduceGarbageRecursion(mEdge& e, const std::vector<bool>& garbage,
                               const Qubit lowerbound,
                               const bool regular = true) {
    if (e.p->v < lowerbound) {
      return e;
    }

    auto f = e;

    std::array<mEdge, NEDGE> edges{};
    std::bitset<NEDGE> handled{};
    for (auto i = 0U; i < NEDGE; ++i) {
      if (!handled.test(i)) {
        if (e.p->e[i].isTerminal()) {
          edges[i] = e.p->e[i];
        } else {
          edges[i] =
              reduceGarbageRecursion(f.p->e[i], garbage, lowerbound, regular);
          for (auto j = i + 1; j < NEDGE; ++j) {
            if (e.p->e[i].p == e.p->e[j].p) {
              edges[j] = edges[i];
              handled.set(j);
            }
          }
        }
        handled.set(i);
      }
    }
    f = makeDDNode(f.p->v, edges);

    // something to reduce for this qubit
    if (garbage[f.p->v]) {
      if (regular) {
        if (f.p->e[2].w != Complex::zero || f.p->e[3].w != Complex::zero) {
          mEdge g{};
          if (f.p->e[0].w == Complex::zero && f.p->e[2].w != Complex::zero) {
            g = f.p->e[2];
          } else if (f.p->e[2].w != Complex::zero) {
            g = add(f.p->e[0], f.p->e[2]);
          } else {
            g = f.p->e[0];
          }
          mEdge h{};
          if (f.p->e[1].w == Complex::zero && f.p->e[3].w != Complex::zero) {
            h = f.p->e[3];
          } else if (f.p->e[3].w != Complex::zero) {
            h = add(f.p->e[1], f.p->e[3]);
          } else {
            h = f.p->e[1];
          }
          f = makeDDNode(e.p->v, std::array{g, h, mEdge::zero, mEdge::zero});
        }
      } else {
        if (f.p->e[1].w != Complex::zero || f.p->e[3].w != Complex::zero) {
          mEdge g{};
          if (f.p->e[0].w == Complex::zero && f.p->e[1].w != Complex::zero) {
            g = f.p->e[1];
          } else if (f.p->e[1].w != Complex::zero) {
            g = add(f.p->e[0], f.p->e[1]);
          } else {
            g = f.p->e[0];
          }
          mEdge h{};
          if (f.p->e[2].w == Complex::zero && f.p->e[3].w != Complex::zero) {
            h = f.p->e[3];
          } else if (f.p->e[3].w != Complex::zero) {
            h = add(f.p->e[2], f.p->e[3]);
          } else {
            h = f.p->e[2];
          }
          f = makeDDNode(e.p->v, std::array{g, mEdge::zero, h, mEdge::zero});
        }
      }
    }
    f.w = cn.lookup(cn.mulTemp(f.w, e.w));

    // Quick-fix for normalization bug
    if (ComplexNumbers::mag2(f.w) > 1.0) {
      f.w = Complex::one;
    }

    return f;
  }

  ///
  /// Vector and matrix extraction from DDs
  ///
public:
  /// Get a single element of the vector or matrix represented by the dd with
  /// root edge e \tparam Edge type of edge to use (vector or matrix) \param e
  /// edge to traverse \param elements string {0, 1, 2, 3}^n describing which
  /// outgoing edge should be followed
  ///        (for vectors entries are limited to 0 and 1)
  ///        If string is longer than required, the additional characters are
  ///        ignored.
  /// \return the complex amplitude of the specified element
  template <class Edge>
  ComplexValue getValueByPath(const Edge& e, const std::string& elements) {
    if (e.isTerminal()) {
      return {RealNumber::val(e.w.r), RealNumber::val(e.w.i)};
    }

    auto c = cn.getTemporary(1, 0);
    auto r = e;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-do-while)
    do {
      ComplexNumbers::mul(c, c, r.w);
      auto tmp = static_cast<std::size_t>(elements.at(r.p->v) - '0');
      assert(tmp <= r.p->e.size());
      r = r.p->e.at(tmp);
    } while (!r.isTerminal());
    ComplexNumbers::mul(c, c, r.w);

    return {RealNumber::val(c.r), RealNumber::val(c.i)};
  }

  template <class Edge>
  ComplexValue getValueByPathPar(const Edge& e, const std::string& elements) {
    if (e.isTerminal()) {
      return {RealNumber::val(e.w.r), RealNumber::val(e.w.i)};
    }

    fp c_real = 1.0;
    fp c_imag = 0;
    auto r = e;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-do-while)
    do {
      fp rw_real = RealNumber::val(r.w.r);
      fp rw_imag = RealNumber::val(r.w.i);
      fp d_real = c_real * rw_real - c_imag * rw_imag;
      fp d_imag = c_real *rw_imag + c_imag * rw_real;
      c_real = d_real;
      c_imag = d_imag;
      auto tmp = static_cast<std::size_t>(elements.at(r.p->v) - '0');
      assert(tmp <= r.p->e.size());
      r = r.p->e.at(tmp);
    } while (!r.isTerminal());
    fp rw_real = RealNumber::val(r.w.r);
    fp rw_imag = RealNumber::val(r.w.i);
    fp d_real = c_real * rw_real - c_imag * rw_imag;
    fp d_imag = c_real *rw_imag + c_imag * rw_real;
    c_real = d_real;
    c_imag = d_imag;

    return {c_real, c_imag};
  }


  ComplexValue getValueByPath(const vEdge& e, const std::size_t i) {
    if (e.isTerminal()) {
      return {RealNumber::val(e.w.r), RealNumber::val(e.w.i)};
    }
    return getValueByPath(e, Complex::one, i);
  }
  ComplexValue getValueByPath(const vEdge& e, const Complex& amp,
                              const std::size_t i) {
    auto c = cn.mulCached(e.w, amp);

    if (e.isTerminal()) {
      cn.returnToCache(c);
      return {RealNumber::val(c.r), RealNumber::val(c.i)};
    }

    const bool one = (i & (1ULL << e.p->v)) != 0U;

    ComplexValue r{};
    if (!one && !e.p->e[0].w.approximatelyZero()) {
      r = getValueByPath(e.p->e[0], c, i);
    } else if (one && !e.p->e[1].w.approximatelyZero()) {
      r = getValueByPath(e.p->e[1], c, i);
    }
    cn.returnToCache(c);
    return r;
  }
  ComplexValue getValueByPath(const mEdge& e, const std::size_t i,
                              const std::size_t j) {
    if (e.isTerminal()) {
      return {RealNumber::val(e.w.r), RealNumber::val(e.w.i)};
    }
    return getValueByPath(e, Complex::one, i, j);
  }
  ComplexValue getValueByPath(const mEdge& e, const Complex& amp,
                              const std::size_t i, const std::size_t j) {
    auto c = cn.mulCached(e.w, amp);

    if (e.isTerminal()) {
      cn.returnToCache(c);
      return {RealNumber::val(c.r), RealNumber::val(c.i)};
    }

    const bool row = (i & (1ULL << e.p->v)) != 0U;
    const bool col = (j & (1ULL << e.p->v)) != 0U;

    ComplexValue r{};
    if (!row && !col && !e.p->e[0].w.approximatelyZero()) {
      r = getValueByPath(e.p->e[0], c, i, j);
    } else if (!row && col && !e.p->e[1].w.approximatelyZero()) {
      r = getValueByPath(e.p->e[1], c, i, j);
    } else if (row && !col && !e.p->e[2].w.approximatelyZero()) {
      r = getValueByPath(e.p->e[2], c, i, j);
    } else if (row && col && !e.p->e[3].w.approximatelyZero()) {
      r = getValueByPath(e.p->e[3], c, i, j);
    }
    cn.returnToCache(c);
    return r;
  }

  std::map<std::string, dd::fp>
  getProbVectorFromDensityMatrix(dEdge e, const double measurementThreshold) {
    dEdge::alignDensityEdge(e);
    if (std::pow(2, e.p->v + 1) >=
        static_cast<double>(std::numeric_limits<std::size_t>::max())) {
      throw std::runtime_error(
          std::string{"Density matrix is too large to measure!"});
    }

    const std::size_t statesToMeasure = 2ULL << e.p->v;
    std::map<std::string, dd::fp> measuredResult = {};
    for (std::size_t m = 0; m < statesToMeasure; m++) {
      std::size_t currentResult = m;
      auto globalProbability = RealNumber::val(e.w.r);
      auto resultString = intToString(m, '1', e.p->v + 1);
      dEdge cur = e;
      for (dd::Qubit i = 0; i < e.p->v + 1; ++i) {
        if (cur.isTerminal() || globalProbability <= measurementThreshold) {
          globalProbability = 0;
          break;
        }
        assert(RealNumber::approximatelyZero(cur.p->e.at(0).w.i) &&
               RealNumber::approximatelyZero(cur.p->e.at(3).w.i));
        const auto p0 = RealNumber::val(cur.p->e.at(0).w.r);
        const auto p1 = RealNumber::val(cur.p->e.at(3).w.r);

        if (currentResult % 2 == 0) {
          cur = cur.p->e.at(0);
          globalProbability *= p0;
        } else {
          cur = cur.p->e.at(3);
          globalProbability *= p1;
        }
        currentResult = currentResult >> 1;
      }
      if (globalProbability > 0) { // No need to track probabilities of 0
        measuredResult.insert({resultString, globalProbability});
      }
    }
    return measuredResult;
  }

  [[nodiscard]] std::string intToString(std::size_t targetNumber,
                                        const char value,
                                        const Qubit size) const {
    std::string path(size, '0');
    for (auto i = 1U; i <= size; ++i) {
      if ((targetNumber % 2) != 0U) {
        path[size - i] = value;
      }
      targetNumber = targetNumber >> 1U;
    }
    return path;
  }

  CVec getVector(const vEdge& e) {
    const std::size_t dim = 2ULL << e.p->v;
    // allocate resulting vector
    auto vec = CVec(dim, {0.0, 0.0});
    getVector(e, Complex::one, 0, vec);
    return vec;
  }
  void getVector(const vEdge& e, const Complex& amp, const std::size_t i,
                 CVec& vec) {
    // calculate new accumulated amplitude
    auto c = cn.mulCached(e.w, amp);

    // base case
    if (e.isTerminal()) {
      vec.at(i) = {RealNumber::val(c.r), RealNumber::val(c.i)};
      cn.returnToCache(c);
      return;
    }

    const std::size_t x = i | (1ULL << e.p->v);

    // recursive case
    if (!e.p->e[0].w.approximatelyZero()) {
      getVector(e.p->e[0], c, i, vec);
    }
    if (!e.p->e[1].w.approximatelyZero()) {
      getVector(e.p->e[1], c, x, vec);
    }
    cn.returnToCache(c);
  }

  void printVector(const vEdge& e) {
    const std::size_t element = 2ULL << e.p->v;
    for (auto i = 0ULL; i < element; i++) {
      const auto amplitude = getValueByPath(e, i);
      const auto n = static_cast<std::size_t>(e.p->v) + 1U;
      for (auto j = n; j > 0; --j) {
        std::cout << ((i >> (j - 1)) & 1ULL);
      }
      constexpr auto precision = 3;
      // set fixed width to maximum of a printed number
      // (-) 0.precision plus/minus 0.precision i
      constexpr auto width = 1 + 2 + precision + 1 + 2 + precision + 1;
      std::cout << ": " << std::setw(width)
                << ComplexValue::toString(amplitude.r, amplitude.i, false,
                                          precision)
                << "\n";
    }
    std::cout << std::flush;
  }

  void printMatrix(const mEdge& e) {
    const std::size_t element = 2ULL << e.p->v;
    for (auto i = 0ULL; i < element; i++) {
      for (auto j = 0ULL; j < element; j++) {
        const auto amplitude = getValueByPath(e, i, j);
        constexpr auto precision = 3;
        // set fixed width to maximum of a printed number
        // (-) 0.precision plus/minus 0.precision i
        constexpr auto width = 1 + 2 + precision + 1 + 2 + precision + 1;
        std::cout << std::setw(width)
                  << ComplexValue::toString(amplitude.r, amplitude.i, false,
                                            precision)
                  << " ";
      }
      std::cout << "\n";
    }
    std::cout << std::flush;
  }

  CMat getMatrix(const mEdge& e) {
    const std::size_t dim = 2ULL << e.p->v;
    // allocate resulting matrix
    auto mat = CMat(dim, CVec(dim, {0.0, 0.0}));
    getMatrix(e, Complex::one, 0, 0, mat);
    return mat;
  }
  void getMatrix(const mEdge& e, const Complex& amp, const std::size_t i,
                 const std::size_t j, CMat& mat) {
    // calculate new accumulated amplitude
    auto c = cn.mulCached(e.w, amp);

    // base case
    if (e.isTerminal()) {
      mat.at(i).at(j) = {RealNumber::val(c.r), RealNumber::val(c.i)};
      cn.returnToCache(c);
      return;
    }

    const std::size_t x = i | (1ULL << e.p->v);
    const std::size_t y = j | (1ULL << e.p->v);

    // recursive case
    if (!e.p->e[0].w.approximatelyZero()) {
      getMatrix(e.p->e[0], c, i, j, mat);
    }
    if (!e.p->e[1].w.approximatelyZero()) {
      getMatrix(e.p->e[1], c, i, y, mat);
    }
    if (!e.p->e[2].w.approximatelyZero()) {
      getMatrix(e.p->e[2], c, x, j, mat);
    }
    if (!e.p->e[3].w.approximatelyZero()) {
      getMatrix(e.p->e[3], c, x, y, mat);
    }
    cn.returnToCache(c);
  }

  CMat getDensityMatrix(dEdge& e) {
    dEdge::applyDmChangesToEdge(e);
    const std::size_t dim = 2ULL << e.p->v;
    // allocate resulting matrix
    auto mat = CMat(dim, CVec(dim, {0.0, 0.0}));
    getDensityMatrix(e, Complex::one, 0, 0, mat);
    dd::dEdge::revertDmChangesToEdge(e);
    return mat;
  }

  void getDensityMatrix(dEdge& e, const Complex& amp, const std::size_t i,
                        const std::size_t j, CMat& mat) {
    // calculate new accumulated amplitude
    auto c = cn.mulCached(e.w, amp);

    // base case
    if (e.isTerminal()) {
      mat.at(i).at(j) = {RealNumber::val(c.r), RealNumber::val(c.i)};
      cn.returnToCache(c);
      return;
    }

    const std::size_t x = i | (1ULL << e.p->v);
    const std::size_t y = j | (1ULL << e.p->v);

    // recursive case
    if (!e.p->e[0].w.approximatelyZero()) {
      dEdge::applyDmChangesToEdge(e.p->e[0]);
      getDensityMatrix(e.p->e[0], c, i, j, mat);
      dd::dEdge::revertDmChangesToEdge(e.p->e[0]);
    }
    if (!e.p->e[1].w.approximatelyZero()) {
      dEdge::applyDmChangesToEdge(e.p->e[1]);
      getDensityMatrix(e.p->e[1], c, i, y, mat);
      dd::dEdge::revertDmChangesToEdge(e.p->e[1]);
    }
    if (!e.p->e[2].w.approximatelyZero()) {
      dEdge::applyDmChangesToEdge(e.p->e[2]);
      getDensityMatrix(e.p->e[2], c, x, j, mat);
      dd::dEdge::revertDmChangesToEdge(e.p->e[2]);
    }
    if (!e.p->e[3].w.approximatelyZero()) {
      dEdge::applyDmChangesToEdge(e.p->e[3]);
      getDensityMatrix(e.p->e[3], c, x, y, mat);
      dd::dEdge::revertDmChangesToEdge(e.p->e[3]);
    }

    cn.returnToCache(c);
  }

  void exportAmplitudesRec(const vEdge& edge, std::ostream& oss,
                           const std::string& path, Complex& amplitude,
                           const std::size_t level, const bool binary = false) {
    if (edge.isTerminal()) {
      auto amp = cn.getTemporary();
      dd::ComplexNumbers::mul(amp, amplitude, edge.w);
      for (std::size_t i = 0; i < (1ULL << level); i++) {
        if (binary) {
          amp.writeBinary(oss);
        } else {
          oss << amp.toString(false, 16) << "\n";
        }
      }

      return;
    }

    auto a = cn.mulCached(amplitude, edge.w);
    exportAmplitudesRec(edge.p->e[0], oss, path + "0", a, level - 1, binary);
    exportAmplitudesRec(edge.p->e[1], oss, path + "1", a, level - 1, binary);
    cn.returnToCache(a);
  }
  void exportAmplitudes(const vEdge& edge, std::ostream& oss,
                        const std::size_t nq, const bool binary = false) {
    if (edge.isTerminal()) {
      // TODO special treatment
      return;
    }
    auto weight = cn.getCached(1., 0.);
    exportAmplitudesRec(edge, oss, "", weight, nq, binary);
    cn.returnToCache(weight);
  }
  void exportAmplitudes(const vEdge& edge, const std::string& outputFilename,
                        const std::size_t nq, const bool binary = false) {
    std::ofstream init(outputFilename);
    std::ostringstream oss{};

    exportAmplitudes(edge, oss, nq, binary);

    init << oss.str() << std::flush;
    init.close();
  }

  void exportAmplitudesRec(const vEdge& edge,
                           std::vector<std::complex<dd::fp>>& amplitudes,
                           Complex& amplitude, const std::size_t level,
                           std::size_t idx) {
    if (edge.isTerminal()) {
      auto amp = cn.getTemporary();
      dd::ComplexNumbers::mul(amp, amplitude, edge.w);
      idx <<= level;
      for (std::size_t i = 0; i < (1ULL << level); i++) {
        amplitudes[idx++] = std::complex<dd::fp>{RealNumber::val(amp.r),
                                                 RealNumber::val(amp.i)};
      }

      return;
    }

    auto a = cn.mulCached(amplitude, edge.w);
    exportAmplitudesRec(edge.p->e[0], amplitudes, a, level - 1, idx << 1);
    exportAmplitudesRec(edge.p->e[1], amplitudes, a, level - 1,
                        (idx << 1) | 1ULL);
    cn.returnToCache(a);
  }
  void exportAmplitudes(const vEdge& edge,
                        std::vector<std::complex<dd::fp>>& amplitudes,
                        const std::size_t nq) {
    if (edge.isTerminal()) {
      // TODO special treatment
      return;
    }
    auto weight = cn.getCached(1., 0.);
    exportAmplitudesRec(edge, amplitudes, weight, nq, 0);
    cn.returnToCache(weight);
  }

  void addAmplitudesRec(const vEdge& edge,
                        std::vector<std::complex<dd::fp>>& amplitudes,
                        ComplexValue& amplitude, const std::size_t level,
                        std::size_t idx) {
    const auto ar = RealNumber::val(edge.w.r);
    const auto ai = RealNumber::val(edge.w.i);
    ComplexValue amp{ar * amplitude.r - ai * amplitude.i,
                     ar * amplitude.i + ai * amplitude.r};

    if (edge.isTerminal()) {
      idx <<= level;
      for (std::size_t i = 0; i < (1ULL << level); i++) {
        auto temp = std::complex<dd::fp>{amp.r + amplitudes[idx].real(),
                                         amp.i + amplitudes[idx].imag()};
        amplitudes[idx++] = temp;
      }

      return;
    }

    addAmplitudesRec(edge.p->e[0], amplitudes, amp, level - 1, idx << 1);
    addAmplitudesRec(edge.p->e[1], amplitudes, amp, level - 1, idx << 1 | 1ULL);
  }
  void addAmplitudes(const vEdge& edge,
                     std::vector<std::complex<dd::fp>>& amplitudes,
                     const std::size_t nq) {
    if (edge.isTerminal()) {
      // TODO special treatment
      return;
    }
    ComplexValue a{1., 0.};
    addAmplitudesRec(edge, amplitudes, a, nq, 0);
  }

  // transfers a decision diagram from another package to this package
  template <class Edge> Edge transfer(Edge& original) {
    // POST ORDER TRAVERSAL USING ONE STACK
    // https://www.geeksforgeeks.org/iterative-postorder-traversal-using-stack/
    Edge root{};
    std::stack<Edge*> stack;

    std::unordered_map<decltype(original.p), decltype(original.p)> mappedNode{};

    Edge* currentEdge = &original;
    if (!currentEdge->isTerminal()) {
      constexpr std::size_t n = std::tuple_size_v<decltype(original.p->e)>;
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-do-while)
      do {
        while (currentEdge != nullptr && !currentEdge->isTerminal()) {
          for (std::size_t i = n - 1; i > 0; --i) {
            auto& edge = currentEdge->p->e[i];
            if (edge.isTerminal()) {
              continue;
            }
            if (edge.w.approximatelyZero()) {
              continue;
            }
            if (mappedNode.find(edge.p) != mappedNode.end()) {
              continue;
            }

            // non-zero edge to be included
            stack.push(&edge);
          }
          stack.push(currentEdge);
          currentEdge = &currentEdge->p->e[0];
        }
        currentEdge = stack.top();
        stack.pop();

        bool hasChild = false;
        for (std::size_t i = 1; i < n && !hasChild; ++i) {
          auto& edge = currentEdge->p->e[i];
          if (edge.w.approximatelyZero()) {
            continue;
          }
          if (mappedNode.find(edge.p) != mappedNode.end()) {
            continue;
          }
          hasChild = edge.p == stack.top()->p;
        }

        if (hasChild) {
          Edge* temp = stack.top();
          stack.pop();
          stack.push(currentEdge);
          currentEdge = temp;
        } else {
          if (mappedNode.find(currentEdge->p) != mappedNode.end()) {
            currentEdge = nullptr;
            continue;
          }
          std::array<Edge, n> edges{};
          for (std::size_t i = 0; i < n; i++) {
            if (currentEdge->p->e[i].isTerminal()) {
              edges[i].p = currentEdge->p->e[i].p;
            } else {
              edges[i].p = mappedNode[currentEdge->p->e[i].p];
            }
            edges[i].w = cn.lookup(currentEdge->p->e[i].w);
          }
          root = makeDDNode(currentEdge->p->v, edges);
          mappedNode[currentEdge->p] = root.p;
          currentEdge = nullptr;
        }
      } while (!stack.empty());
      root.w = cn.lookup(cn.mulTemp(original.w, root.w));
    } else {
      root.p = original.p; // terminal -> static
      root.w = cn.lookup(original.w);
    }
    return root;
  }

  ///
  /// Deserialization
  /// Note: do not rely on the binary format being portable across different
  /// architectures/platforms
  ///

  template <class Node, class Edge = Edge<Node>,
            std::size_t N = std::tuple_size_v<decltype(Node::e)>>
  Edge deserialize(std::istream& is, const bool readBinary = false) {
    auto result = Edge::zero;
    ComplexValue rootweight{};

    std::unordered_map<std::int64_t, Node*> nodes{};
    std::int64_t nodeIndex{};
    Qubit v{};
    std::array<ComplexValue, N> edgeWeights{};
    std::array<std::int64_t, N> edgeIndices{};
    edgeIndices.fill(-2);

    if (readBinary) {
      std::remove_const_t<decltype(SERIALIZATION_VERSION)> version{};
      is.read(reinterpret_cast<char*>(&version),
              sizeof(decltype(SERIALIZATION_VERSION)));
      if (version != SERIALIZATION_VERSION) {
        throw std::runtime_error(
            "Wrong Version of serialization file version. version of file: " +
            std::to_string(version) +
            "; current version: " + std::to_string(SERIALIZATION_VERSION));
      }

      if (!is.eof()) {
        rootweight.readBinary(is);
      }

      while (is.read(reinterpret_cast<char*>(&nodeIndex),
                     sizeof(decltype(nodeIndex)))) {
        is.read(reinterpret_cast<char*>(&v), sizeof(decltype(v)));
        for (std::size_t i = 0U; i < N; i++) {
          is.read(reinterpret_cast<char*>(&edgeIndices[i]),
                  sizeof(decltype(edgeIndices[i])));
          edgeWeights[i].readBinary(is);
        }
        result = deserializeNode(nodeIndex, v, edgeIndices, edgeWeights, nodes);
      }
    } else {
      std::string version;
      std::getline(is, version);
      if (std::stoi(version) != SERIALIZATION_VERSION) {
        throw std::runtime_error(
            "Wrong Version of serialization file version. version of file: " +
            version +
            "; current version: " + std::to_string(SERIALIZATION_VERSION));
      }

      const std::string complexRealRegex =
          R"(([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?(?![ \d\.]*(?:[eE][+-])?\d*[iI]))?)";
      const std::string complexImagRegex =
          R"(( ?[+-]? ?(?:(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)?[iI])?)";
      const std::string edgeRegex =
          " \\(((-?\\d+) (" + complexRealRegex + complexImagRegex + "))?\\)";
      const std::regex complexWeightRegex(complexRealRegex + complexImagRegex);

      std::string lineConstruct = "(\\d+) (\\d+)";
      for (std::size_t i = 0U; i < N; ++i) {
        lineConstruct += "(?:" + edgeRegex + ")";
      }
      lineConstruct += " *(?:#.*)?";
      const std::regex lineRegex(lineConstruct);
      std::smatch m;

      std::string line;
      if (std::getline(is, line)) {
        if (!std::regex_match(line, m, complexWeightRegex)) {
          throw std::runtime_error("Regex did not match second line: " + line);
        }
        rootweight.fromString(m.str(1), m.str(2));
      }

      while (std::getline(is, line)) {
        if (line.empty() || line.size() == 1) {
          continue;
        }

        if (!std::regex_match(line, m, lineRegex)) {
          throw std::runtime_error("Regex did not match line: " + line);
        }

        // match 1: node_idx
        // match 2: qubit_idx

        // repeats for every edge
        // match 3: edge content
        // match 4: edge_target_idx
        // match 5: real + imag (without i)
        // match 6: real
        // match 7: imag (without i)
        nodeIndex = std::stoi(m.str(1));
        v = static_cast<Qubit>(std::stoi(m.str(2)));

        for (auto edgeIdx = 3U, i = 0U; i < N; i++, edgeIdx += 5) {
          if (m.str(edgeIdx).empty()) {
            continue;
          }

          edgeIndices[i] = std::stoi(m.str(edgeIdx + 1));
          edgeWeights[i].fromString(m.str(edgeIdx + 3), m.str(edgeIdx + 4));
        }

        result = deserializeNode(nodeIndex, v, edgeIndices, edgeWeights, nodes);
      }
    }

    auto w = cn.getTemporary(rootweight.r, rootweight.i);
    ComplexNumbers::mul(w, w, result.w);
    result.w = cn.lookup(w);

    return result;
  }

  template <class Node, class Edge = Edge<Node>>
  Edge deserialize(const std::string& inputFilename, const bool readBinary) {
    auto ifs = std::ifstream(inputFilename, std::ios::binary);

    if (!ifs.good()) {
      throw std::invalid_argument("Cannot open serialized file: " +
                                  inputFilename);
    }

    return deserialize<Node>(ifs, readBinary);
  }

private:
  template <class Node, class Edge = Edge<Node>,
            std::size_t N = std::tuple_size_v<decltype(Node::e)>>
  Edge deserializeNode(const std::int64_t index, const Qubit v,
                       std::array<std::int64_t, N>& edgeIdx,
                       const std::array<ComplexValue, N>& edgeWeight,
                       std::unordered_map<std::int64_t, Node*>& nodes) {
    if (index == -1) {
      return Edge::zero;
    }

    std::array<Edge, N> edges{};
    for (auto i = 0U; i < N; ++i) {
      if (edgeIdx[i] == -2) {
        edges[i] = Edge::zero;
      } else {
        if (edgeIdx[i] == -1) {
          edges[i] = Edge::one;
        } else {
          edges[i].p = nodes[edgeIdx[i]];
        }
        edges[i].w = cn.lookup(edgeWeight[i]);
      }
    }

    auto newedge = makeDDNode(v, edges);
    nodes[index] = newedge.p;

    // reset
    edgeIdx.fill(-2);

    return newedge;
  }

  ///
  /// Debugging
  ///
public:
  template <class Node> void debugnode(const Node* p) const {
    if (Node::isTerminal(p)) {
      std::clog << "terminal\n";
      return;
    }
    std::clog << "Debug node: " << debugnodeLine(p) << "\n";
    for (const auto& edge : p->e) {
      std::clog << "  " << std::hexfloat << std::setw(22)
                << RealNumber::val(edge.w.r) << " " << std::setw(22)
                << RealNumber::val(edge.w.i) << std::defaultfloat << "i --> "
                << debugnodeLine(edge.p) << "\n";
    }
    std::clog << std::flush;
  }

  template <class Node> std::string debugnodeLine(const Node* p) const {
    if (Node::isTerminal(p)) {
      return "terminal";
    }
    std::stringstream sst;
    sst << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(p) << std::dec
        << "[v=" << static_cast<std::int64_t>(p->v) << " ref=" << p->ref
        << " hash=" << UniqueTable<Node>::hash(p) << "]";
    return sst.str();
  }

  template <class Edge> bool isLocallyConsistent(const Edge& e) {
    // NOLINTNEXTLINE(clang-diagnostic-float-equal)
    assert(Complex::one.r->value == 1 && Complex::one.i->value == 0);
    // NOLINTNEXTLINE(clang-diagnostic-float-equal)
    assert(Complex::zero.r->value == 0 && Complex::zero.i->value == 0);

    const bool result = isLocallyConsistent2(e);
    return result;
  }

  template <class Edge> bool isGloballyConsistent(const Edge& e) {
    std::map<RealNumber*, std::size_t> weightCounter{};
    std::map<decltype(e.p), std::size_t> nodeCounter{};
    fillConsistencyCounter(e, weightCounter, nodeCounter);
    checkConsistencyCounter(e, weightCounter, nodeCounter);
    return true;
  }

private:
  template <class Edge> bool isLocallyConsistent2(const Edge& e) {
    const auto* ptrR = RealNumber::getAlignedPointer(e.w.r);
    const auto* ptrI = RealNumber::getAlignedPointer(e.w.i);

    if ((ptrR->ref == 0 || ptrI->ref == 0) && e.w != Complex::one &&
        e.w != Complex::zero) {
      std::clog << "\nLOCAL INCONSISTENCY FOUND\nOffending Number: " << e.w
                << " (" << ptrR->ref << ", " << ptrI->ref << ")\n\n";
      debugnode(e.p);
      return false;
    }

    if (e.isTerminal()) {
      return true;
    }

    if (!e.isTerminal() && e.p->ref == 0) {
      std::clog << "\nLOCAL INCONSISTENCY FOUND: RC==0\n";
      debugnode(e.p);
      return false;
    }

    for (const auto& child : e.p->e) {
      if (!child.isTerminal() && child.p->v + 1 != e.p->v) {
        std::clog << "\nLOCAL INCONSISTENCY FOUND: Wrong V\n";
        debugnode(e.p);
        return false;
      }
      if (!child.isTerminal() && child.p->ref == 0) {
        std::clog << "\nLOCAL INCONSISTENCY FOUND: RC==0\n";
        debugnode(e.p);
        return false;
      }
      if (!isLocallyConsistent2(child)) {
        return false;
      }
    }
    return true;
  }

  template <class Edge>
  void
  fillConsistencyCounter(const Edge& edge,
                         std::map<RealNumber*, std::size_t>& weightMap,
                         std::map<decltype(edge.p), std::size_t>& nodeMap) {
    weightMap[RealNumber::getAlignedPointer(edge.w.r)]++;
    weightMap[RealNumber::getAlignedPointer(edge.w.i)]++;

    if (edge.isTerminal()) {
      return;
    }
    nodeMap[edge.p]++;
    for (auto& child : edge.p->e) {
      if (nodeMap[child.p] == 0) {
        fillConsistencyCounter(child, weightMap, nodeMap);
      } else {
        nodeMap[child.p]++;
        weightMap[RealNumber::getAlignedPointer(child.w.r)]++;
        weightMap[RealNumber::getAlignedPointer(child.w.i)]++;
      }
    }
  }

  template <class Edge>
  void checkConsistencyCounter(
      const Edge& edge, const std::map<RealNumber*, std::size_t>& weightMap,
      const std::map<decltype(edge.p), std::size_t>& nodeMap) {
    auto* rPtr = RealNumber::getAlignedPointer(edge.w.r);
    auto* iPtr = RealNumber::getAlignedPointer(edge.w.i);

    if (weightMap.at(rPtr) > rPtr->ref && !constants::isStaticNumber(rPtr)) {
      std::clog << "\nOffending weight: " << edge.w << "\n";
      std::clog << "Bits: " << std::hexfloat << RealNumber::val(edge.w.r)
                << "r " << RealNumber::val(edge.w.i) << std::defaultfloat
                << "i\n";
      debugnode(edge.p);
      throw std::runtime_error("Ref-Count mismatch for " +
                               std::to_string(rPtr->value) +
                               "(r): " + std::to_string(weightMap.at(rPtr)) +
                               " occurrences in DD but Ref-Count is only " +
                               std::to_string(rPtr->ref));
    }

    if (weightMap.at(iPtr) > iPtr->ref && !constants::isStaticNumber(iPtr)) {
      std::clog << "\nOffending weight: " << edge.w << "\n";
      std::clog << "Bits: " << std::hexfloat << RealNumber::val(edge.w.r)
                << "r " << RealNumber::val(edge.w.i) << std::defaultfloat
                << "i\n";
      debugnode(edge.p);
      throw std::runtime_error("Ref-Count mismatch for " +
                               std::to_string(iPtr->value) +
                               "(i): " + std::to_string(weightMap.at(iPtr)) +
                               " occurrences in DD but Ref-Count is only " +
                               std::to_string(iPtr->ref));
    }

    if (edge.isTerminal()) {
      return;
    }

    if (nodeMap.at(edge.p) != edge.p->ref) {
      debugnode(edge.p);
      throw std::runtime_error(
          "Ref-Count mismatch for node: " + std::to_string(nodeMap.at(edge.p)) +
          " occurrences in DD but Ref-Count is " + std::to_string(edge.p->ref));
    }
    for (auto child : edge.p->e) {
      if (!child.isTerminal() && child.p->v != edge.p->v - 1) {
        std::clog << "child.p->v == " << child.p->v << "\n";
        std::clog << " edge.p->v == " << edge.p->v << "\n";
        debugnode(child.p);
        debugnode(edge.p);
        throw std::runtime_error("Variable level ordering seems wrong");
      }
      checkConsistencyCounter(child, weightMap, nodeMap);
    }
  }

  ///
  /// Printing and Statistics
  ///
public:
  // print information on package and its members
  static void printInformation() {
    std::cout << "\n  compiled: " << __DATE__ << " " << __TIME__
              << "\n  Complex size: " << sizeof(Complex) << " bytes (aligned "
              << alignof(Complex) << " bytes)"
              << "\n  ComplexValue size: " << sizeof(ComplexValue)
              << " bytes (aligned " << alignof(ComplexValue) << " bytes)"
              << "\n  ComplexNumbers size: " << sizeof(ComplexNumbers)
              << " bytes (aligned " << alignof(ComplexNumbers) << " bytes)"
              << "\n  vEdge size: " << sizeof(vEdge) << " bytes (aligned "
              << alignof(vEdge) << " bytes)"
              << "\n  vNode size: " << sizeof(vNode) << " bytes (aligned "
              << alignof(vNode) << " bytes)"
              << "\n  mEdge size: " << sizeof(mEdge) << " bytes (aligned "
              << alignof(mEdge) << " bytes)"
              << "\n  mNode size: " << sizeof(mNode) << " bytes (aligned "
              << alignof(mNode) << " bytes)"
              << "\n  dEdge size: " << sizeof(dEdge) << " bytes (aligned "
              << alignof(dEdge) << " bytes)"
              << "\n  dNode size: " << sizeof(dNode) << " bytes (aligned "
              << alignof(dNode) << " bytes)"
              << "\n  CT Vector Add size: "
              << sizeof(typename decltype(vectorAdd)::Entry)
              << " bytes (aligned "
              << alignof(typename decltype(vectorAdd)::Entry) << " bytes)"
              << "\n  CT Matrix Add size: "
              << sizeof(typename decltype(matrixAdd)::Entry)
              << " bytes (aligned "
              << alignof(typename decltype(matrixAdd)::Entry) << " bytes)"
              << "\n  CT Matrix Transpose size: "
              << sizeof(typename decltype(matrixTranspose)::Entry)
              << " bytes (aligned "
              << alignof(typename decltype(matrixTranspose)::Entry) << " bytes)"
              << "\n  CT Conjugate Matrix Transpose size: "
              << sizeof(typename decltype(conjugateMatrixTranspose)::Entry)
              << " bytes (aligned "
              << alignof(typename decltype(conjugateMatrixTranspose)::Entry)
              << " bytes)"
              << "\n  CT Matrix Multiplication size: "
              << sizeof(typename decltype(matrixMatrixMultiplication)::Entry)
              << " bytes (aligned "
              << alignof(typename decltype(matrixMatrixMultiplication)::Entry)
              << " bytes)"
              << "\n  CT Matrix Vector Multiplication size: "
              << sizeof(typename decltype(matrixVectorMultiplication)::Entry)
              << " bytes (aligned "
              << alignof(typename decltype(matrixVectorMultiplication)::Entry)
              << " bytes)"
              << "\n  CT Vector Inner Product size: "
              << sizeof(typename decltype(vectorInnerProduct)::Entry)
              << " bytes (aligned "
              << alignof(typename decltype(vectorInnerProduct)::Entry)
              << " bytes)"
              << "\n  CT Vector Kronecker size: "
              << sizeof(typename decltype(vectorKronecker)::Entry)
              << " bytes (aligned "
              << alignof(typename decltype(vectorKronecker)::Entry) << " bytes)"
              << "\n  CT Matrix Kronecker size: "
              << sizeof(typename decltype(matrixKronecker)::Entry)
              << " bytes (aligned "
              << alignof(typename decltype(matrixKronecker)::Entry) << " bytes)"
              << "\n  Package size: " << sizeof(SwitchPackage) << " bytes (aligned "
              << alignof(SwitchPackage) << " bytes)"
              << "\n"
              << std::flush;
  }

  // print unique and compute table statistics
  void statistics() {
    std::cout << "DD statistics:\n";
    std::cout << "[vUniqueTable] " << vUniqueTable.getStats() << "\n";
    std::cout << "[mUniqueTable] " << mUniqueTable.getStats() << "\n";
    std::cout << "[dUniqueTable] " << dUniqueTable.getStats() << "\n";
    std::cout << "[cUniqueTable] " << cUniqueTable.getStats() << "\n";
    std::cout << "[CT Vector Add] ";
    vectorAdd.printStatistics();
    std::cout << "[CT Matrix Add] ";
    matrixAdd.printStatistics();
    std::cout << "[CT Matrix Transpose] ";
    matrixTranspose.printStatistics();
    std::cout << "[CT Conjugate Matrix Transpose] ";
    conjugateMatrixTranspose.printStatistics();
    std::cout << "[CT Matrix Multiplication] ";
    matrixMatrixMultiplication.printStatistics();
    std::cout << "[CT Matrix Vector Multiplication] ";
    matrixVectorMultiplication.printStatistics();
    std::cout << "[CT Inner Product] ";
    vectorInnerProduct.printStatistics();
    std::cout << "[CT Vector Kronecker] ";
    vectorKronecker.printStatistics();
    std::cout << "[CT Matrix Kronecker] ";
    matrixKronecker.printStatistics();
    std::cout << "[Stochastic Noise Table] ";
    stochasticNoiseOperationCache.printStatistics();
    std::cout << "[CT Density Add] ";
    densityAdd.printStatistics();
    std::cout << "[CT Density Mul] ";
    densityDensityMultiplication.printStatistics();
    std::cout << "[CT Density Noise] ";
    densityNoise.printStatistics();
  }
};

} // namespace dd
