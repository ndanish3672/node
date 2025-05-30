// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_OPERATION_MATCHER_H_
#define V8_COMPILER_TURBOSHAFT_OPERATION_MATCHER_H_

#include <limits>
#include <optional>
#include <type_traits>

#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"

namespace v8::internal::compiler::turboshaft {

class OperationMatcher;

namespace detail {
template <typename T, bool HasConstexpr>
struct ValueMatch {
  struct Wildcard {};
  using constexpr_type = typename v_traits<T>::constexpr_type;

  ValueMatch() : v_(Wildcard{}) {}
  ValueMatch(OpIndex index) : v_(index) {}   // NOLINT(runtime/explicit)
  ValueMatch(OpIndex* index) : v_(index) {}  // NOLINT(runtime/explicit)
  ValueMatch(V<T>* index) : v_(index) {}     // NOLINT(runtime/explicit)
  ValueMatch(constexpr_type constant)        // NOLINT(runtime/explicit)
      : v_(constant) {}

  inline bool matches(OpIndex matched, const OperationMatcher* matcher);
  inline void bind(OpIndex matched, const OperationMatcher* matcher);

  std::variant<Wildcard, OpIndex, OpIndex*, constexpr_type> v_;
};

template <typename T>
struct ValueMatch<T, false> {
  struct Wildcard {};

  ValueMatch() : v_(Wildcard{}) {}
  ValueMatch(OpIndex index) : v_(index) {}   // NOLINT(runtime/explicit)
  ValueMatch(OpIndex* index) : v_(index) {}  // NOLINT(runtime/explicit)
  ValueMatch(V<T>* index) : v_(index) {}     // NOLINT(runtime/explicit)

  inline bool matches(OpIndex matched, const OperationMatcher* matcher) {
    if (v_.index() == 1) return std::get<1>(v_) == matched;
    return true;
  }

  inline void bind(OpIndex matched, const OperationMatcher* matcher) {
    DCHECK(matches(matched, matcher));
    if (v_.index() == 2) *std::get<2>(v_) = matched;
  }

  std::variant<Wildcard, OpIndex, OpIndex*> v_;
};

template <typename T>
struct OptionMatch {
  struct Wildcard {};

  OptionMatch() : v_(Wildcard{}) {}
  OptionMatch(const T& value) : v_(value) {}  // NOLINT(runtime/explicit)
  OptionMatch(T* value) : v_(value) {}        // NOLINT(runtime/explicit)

  std::variant<Wildcard, T, T*> v_;

  bool matches(const T& matched) {
    if (v_.index() == 1) return std::get<1>(v_) == matched;
    return true;
  }

  void bind(const T& matched) {
    DCHECK(matches(matched));
    if (v_.index() == 2) *std::get<2>(v_) = matched;
  }
};

}  // namespace detail

class OperationMatcher {
 public:
  template <typename T>
  using VMatch = detail::ValueMatch<T, const_or_v_exists_v<T>>;
  template <typename T>
  using OMatch = detail::OptionMatch<T>;

  explicit OperationMatcher(const Graph& graph) : graph_(graph) {}

  template <class Op>
  bool Is(V<AnyOrNone> op_idx) const {
    return graph_.Get(op_idx).Is<Op>();
  }

  template <class Op>
  const underlying_operation_t<Op>* TryCast(V<AnyOrNone> op_idx) const {
    return graph_.Get(op_idx).TryCast<Op>();
  }

  template <class Op>
  const underlying_operation_t<Op>& Cast(V<AnyOrNone> op_idx) const {
    return graph_.Get(op_idx).Cast<Op>();
  }

  const Operation& Get(V<AnyOrNone> op_idx) const { return graph_.Get(op_idx); }

  V<AnyOrNone> Index(const Operation& op) const { return graph_.Index(op); }

  bool MatchZero(V<Any> matched) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    switch (op->kind) {
      case ConstantOp::Kind::kWord32:
      case ConstantOp::Kind::kWord64:
        return op->integral() == 0;
      case ConstantOp::Kind::kFloat32:
        return op->float32().get_scalar() == 0;
      case ConstantOp::Kind::kFloat64:
        return op->float64().get_scalar() == 0;
      case ConstantOp::Kind::kSmi:
        return op->smi().value() == 0;
      default:
        return false;
    }
  }

  bool MatchIntegralZero(V<Any> matched) const {
    int64_t constant;
    return MatchSignedIntegralConstant(matched, &constant) && constant == 0;
  }

  bool MatchSmiZero(V<Any> matched) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kSmi) return false;
    return op->smi().value() == 0;
  }

  bool MatchFloat32Constant(V<Any> matched, float* constant) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kFloat32) return false;
    *constant = op->storage.float32.get_scalar();
    return true;
  }

  bool MatchFloat32Constant(V<Any> matched, i::Float32* constant) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kFloat32) return false;
    *constant = op->storage.float32;
    return true;
  }

  bool MatchFloat64Constant(V<Any> matched, double* constant) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kFloat64) return false;
    *constant = op->storage.float64.get_scalar();
    return true;
  }

  bool MatchFloat64Constant(V<Any> matched, i::Float64* constant) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kFloat64) return false;
    *constant = op->storage.float64;
    return true;
  }

  bool MatchFloat(V<Any> matched, double* value) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind == ConstantOp::Kind::kFloat64) {
      *value = op->storage.float64.get_scalar();
      return true;
    } else if (op->kind == ConstantOp::Kind::kFloat32) {
      *value = op->storage.float32.get_scalar();
      return true;
    }
    return false;
  }

  bool MatchFloat(V<Any> matched, double value) const {
    double k;
    if (!MatchFloat(matched, &k)) return false;
    return base::bit_cast<uint64_t>(value) == base::bit_cast<uint64_t>(k) ||
           (std::isnan(k) && std::isnan(value));
  }

  bool MatchNaN(V<Float> matched) const {
    double k;
    return MatchFloat(matched, &k) && std::isnan(k);
  }

  bool MatchHeapConstant(V<Any> matched,
                         Handle<HeapObject>* tagged = nullptr) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (!(op->kind == any_of(ConstantOp::Kind::kHeapObject,
                             ConstantOp::Kind::kCompressedHeapObject))) {
      return false;
    }
    if (tagged) {
      *tagged = op->handle();
    }
    return true;
  }

  bool MatchIntegralWordConstant(V<Any> matched, WordRepresentation rep,
                                 uint64_t* unsigned_constant,
                                 int64_t* signed_constant = nullptr) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    switch (op->kind) {
      case ConstantOp::Kind::kWord32:
      case ConstantOp::Kind::kWord64:
      case ConstantOp::Kind::kRelocatableWasmCall:
      case ConstantOp::Kind::kRelocatableWasmStubCall:
        if (rep.value() == WordRepresentation::Word32()) {
          if (unsigned_constant) {
            *unsigned_constant = static_cast<uint32_t>(op->integral());
          }
          if (signed_constant) {
            *signed_constant = static_cast<int32_t>(op->signed_integral());
          }
          return true;
        } else if (rep.value() == WordRepresentation::Word64()) {
          if (unsigned_constant) {
            *unsigned_constant = op->integral();
          }
          if (signed_constant) {
            *signed_constant = op->signed_integral();
          }
          return true;
        }
        return false;
      default:
        return false;
    }
    UNREACHABLE();
  }

  bool MatchIntegralWordConstant(V<Any> matched, WordRepresentation rep,
                                 int64_t* signed_constant) const {
    return MatchIntegralWordConstant(matched, rep, nullptr, signed_constant);
  }

  bool MatchIntegralWord32Constant(V<Any> matched, uint32_t* constant) const {
    if (uint64_t value; MatchIntegralWordConstant(
            matched, WordRepresentation::Word32(), &value)) {
      *constant = static_cast<uint32_t>(value);
      return true;
    }
    return false;
  }

  bool MatchIntegralWord64Constant(V<Any> matched, uint64_t* constant) const {
    return MatchIntegralWordConstant(matched, WordRepresentation::Word64(),
                                     constant);
  }

  bool MatchIntegralWord32Constant(V<Any> matched, uint32_t constant) const {
    if (uint64_t value; MatchIntegralWordConstant(
            matched, WordRepresentation::Word32(), &value)) {
      return static_cast<uint32_t>(value) == constant;
    }
    return false;
  }

  bool MatchIntegralWord64Constant(V<Any> matched, int64_t* constant) const {
    return MatchIntegralWordConstant(matched, WordRepresentation::Word64(),
                                     constant);
  }

  bool MatchIntegralWord32Constant(V<Any> matched, int32_t* constant) const {
    if (int64_t value; MatchIntegralWordConstant(
            matched, WordRepresentation::Word32(), &value)) {
      *constant = static_cast<int32_t>(value);
      return true;
    }
    return false;
  }

  template <typename T = intptr_t>
  bool MatchIntegralWordPtrConstant(V<Any> matched, T* constant) const {
    if constexpr (Is64()) {
      static_assert(sizeof(T) == sizeof(int64_t));
      int64_t v;
      if (!MatchIntegralWord64Constant(matched, &v)) return false;
      *constant = static_cast<T>(v);
      return true;
    } else {
      static_assert(sizeof(T) == sizeof(int32_t));
      int32_t v;
      if (!MatchIntegralWord32Constant(matched, &v)) return false;
      *constant = static_cast<T>(v);
      return true;
    }
  }

  bool MatchSignedIntegralConstant(V<Any> matched, int64_t* constant) const {
    if (const ConstantOp* c = TryCast<ConstantOp>(matched)) {
      if (c->kind == ConstantOp::Kind::kWord32 ||
          c->kind == ConstantOp::Kind::kWord64) {
        *constant = c->signed_integral();
        return true;
      }
    }
    return false;
  }

  bool MatchUnsignedIntegralConstant(V<Any> matched, uint64_t* constant) const {
    if (const ConstantOp* c = TryCast<ConstantOp>(matched)) {
      if (c->kind == ConstantOp::Kind::kWord32 ||
          c->kind == ConstantOp::Kind::kWord64) {
        *constant = c->integral();
        return true;
      }
    }
    return false;
  }

  bool MatchExternalConstant(V<Any> matched,
                             ExternalReference* reference) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kExternal) return false;
    *reference = op->storage.external;
    return true;
  }

  bool MatchWasmStubCallConstant(V<Any> matched, uint64_t* stub_id) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kRelocatableWasmStubCall) {
      return false;
    }
    *stub_id = op->integral();
    return true;
  }

  template <typename T>
  bool MatchChange(V<Any> matched, VMatch<T> input,
                   OMatch<ChangeOp::Kind> kind = {},
                   OMatch<ChangeOp::Assumption> assumption = {},
                   OMatch<RegisterRepresentation> from = {},
                   OMatch<RegisterRepresentation> to = {}) const {
    const ChangeOp* op = TryCast<ChangeOp>(matched);
    if (!op) return false;
    if (input.matches(op->input(), this) && kind.matches(op->kind) &&
        assumption.matches(op->assumption) && from.matches(op->from) &&
        to.matches(op->to)) {
      input.bind(op->input(), this);
      kind.bind(op->kind);
      assumption.bind(op->assumption);
      from.bind(op->from);
      to.bind(op->to);
      return true;
    }
    return false;
  }

  bool MatchTruncateWord64ToWord32(V<Any> matched, VMatch<Word64> input) const {
    return MatchChange<Word64>(matched, input, ChangeOp::Kind::kTruncate, {},
                               RegisterRepresentation::Word64(),
                               RegisterRepresentation::Word32());
  }

  template <typename T>
    requires(IsWord<T>())
  bool MatchWordBinop(V<Any> matched, VMatch<T> left, VMatch<T> right,
                      OMatch<WordBinopOp::Kind> kind = {},
                      OMatch<WordRepresentation> rep = {}) const {
    const WordBinopOp* op = TryCast<WordBinopOp>(matched);
    if (!op) return false;
    if (left.matches(op->left(), this) && right.matches(op->right(), this) &&
        kind.matches(op->kind) && rep.matches(op->rep)) {
      left.bind(op->left(), this);
      right.bind(op->right(), this);
      kind.bind(op->kind);
      rep.bind(op->rep);
      return true;
    }
    return false;
  }

  template <class T>
    requires(IsWord<T>())
  bool MatchWordAdd(V<Any> matched, V<T>* left, V<T>* right,
                    WordRepresentation rep) const {
    return MatchWordBinop<T>(matched, left, right, WordBinopOp::Kind::kAdd,
                             rep);
  }

  template <class T>
    requires(IsWord<T>())
  bool MatchWordSub(V<Any> matched, V<T>* left, V<T>* right,
                    WordRepresentation rep) const {
    return MatchWordBinop<T>(matched, left, right, WordBinopOp::Kind::kSub,
                             rep);
  }

  template <class T>
    requires(IsWord<T>())
  bool MatchWordMul(V<Any> matched, V<T>* left, V<T>* right,
                    WordRepresentation rep) const {
    return MatchWordBinop<T>(matched, left, right, WordBinopOp::Kind::kMul,
                             rep);
  }

  template <class T>
    requires(IsWord<T>())
  bool MatchBitwiseAnd(V<Any> matched, V<T>* left, V<T>* right,
                       WordRepresentation rep) const {
    return MatchWordBinop<T>(matched, left, right,
                             WordBinopOp::Kind::kBitwiseAnd, rep);
  }

  template <class T>
    requires(IsWord<T>())
  bool MatchBitwiseAndWithConstant(V<Any> matched, V<T>* value,
                                   uint64_t* constant,
                                   WordRepresentation rep) const {
    V<T> left, right;
    if (!MatchBitwiseAnd(matched, &left, &right, rep)) return false;
    if (MatchIntegralWordConstant(right, rep, constant)) {
      *value = left;
      return true;
    } else if (MatchIntegralWordConstant(left, rep, constant)) {
      *value = right;
      return true;
    }
    return false;
  }

  template <typename T>
  bool MatchEqual(V<Any> matched, V<T>* left, V<T>* right) const {
    const ComparisonOp* op = TryCast<ComparisonOp>(matched);
    if (!op || op->kind != ComparisonOp::Kind::kEqual || op->rep != V<T>::rep) {
      return false;
    }
    *left = V<T>::Cast(op->left());
    *right = V<T>::Cast(op->right());
    return true;
  }

  bool MatchFloatUnary(V<Any> matched, V<Float>* input, FloatUnaryOp::Kind kind,
                       FloatRepresentation rep) const {
    const FloatUnaryOp* op = TryCast<FloatUnaryOp>(matched);
    if (!op || op->kind != kind || op->rep != rep) return false;
    *input = op->input();
    return true;
  }

  bool MatchFloatRoundDown(V<Any> matched, V<Float>* input,
                           FloatRepresentation rep) const {
    return MatchFloatUnary(matched, input, FloatUnaryOp::Kind::kRoundDown, rep);
  }

  bool MatchFloatBinary(V<Any> matched, V<Float>* left, V<Float>* right,
                        FloatBinopOp::Kind kind,
                        FloatRepresentation rep) const {
    const FloatBinopOp* op = TryCast<FloatBinopOp>(matched);
    if (!op || op->kind != kind || op->rep != rep) return false;
    *left = op->left();
    *right = op->right();
    return true;
  }

  bool MatchFloatSub(V<Any> matched, V<Float>* left, V<Float>* right,
                     FloatRepresentation rep) const {
    return MatchFloatBinary(matched, left, right, FloatBinopOp::Kind::kSub,
                            rep);
  }

  template <class T>
    requires(IsWord<T>())
  bool MatchConstantShift(V<Any> matched, V<T>* input, ShiftOp::Kind* kind,
                          WordRepresentation* rep, int* amount) const {
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && MatchIntegralWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint64_t>(op->rep.bit_width())) {
      *input = op->left<T>();
      *kind = op->kind;
      *rep = op->rep;
      *amount = static_cast<int>(rhs_constant);
      return true;
    }
    return false;
  }

  template <class T>
    requires(IsWord<T>())
  bool MatchConstantShift(V<Any> matched, V<T>* input, ShiftOp::Kind kind,
                          WordRepresentation rep, int* amount) const {
    DCHECK(IsValidTypeFor<T>(rep));
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && op->kind == kind &&
        (op->rep == rep || (ShiftOp::AllowsWord64ToWord32Truncation(kind) &&
                            rep == WordRepresentation::Word32() &&
                            op->rep == WordRepresentation::Word64())) &&
        MatchIntegralWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint64_t>(rep.bit_width())) {
      *input = op->left<T>();
      *amount = static_cast<int>(rhs_constant);
      return true;
    }
    return false;
  }

  template <class T>
    requires(IsWord<T>())
  bool MatchConstantRightShift(V<Any> matched, V<T>* input,
                               WordRepresentation rep, int* amount) const {
    DCHECK(IsValidTypeFor<T>(rep));
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && ShiftOp::IsRightShift(op->kind) && op->rep == rep &&
        MatchIntegralWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint32_t>(rep.bit_width())) {
      *input = op->left<T>();
      *amount = static_cast<int>(rhs_constant);
      return true;
    }
    return false;
  }

  template <class T>
    requires(IsWord<T>())
  bool MatchConstantLeftShift(V<Any> matched, V<T>* input,
                              WordRepresentation rep, int* amount) const {
    DCHECK(IsValidTypeFor<T>(rep));
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && op->kind == ShiftOp::Kind::kShiftLeft && op->rep == rep &&
        MatchIntegralWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint32_t>(rep.bit_width())) {
      *input = op->left<T>();
      *amount = static_cast<int>(rhs_constant);
      return true;
    }
    return false;
  }

  template <class T>
    requires(IsWord<T>())
  bool MatchConstantShiftRightArithmeticShiftOutZeros(V<Any> matched,
                                                      V<T>* input,
                                                      WordRepresentation rep,
                                                      uint16_t* amount) const {
    DCHECK(IsValidTypeFor<T>(rep));
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && op->kind == ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros &&
        op->rep == rep &&
        MatchIntegralWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint64_t>(rep.bit_width())) {
      *input = op->left<T>();
      *amount = static_cast<uint16_t>(rhs_constant);
      return true;
    }
    return false;
  }

  bool MatchPhi(V<Any> matched,
                std::optional<int> input_count = std::nullopt) const {
    if (const PhiOp* phi = TryCast<PhiOp>(matched)) {
      return !input_count.has_value() || phi->input_count == *input_count;
    }
    return false;
  }

  bool MatchPowerOfTwoWordConstant(V<Any> matched, int64_t* ret_cst,
                                   WordRepresentation rep) const {
    int64_t loc_cst;
    if (MatchIntegralWordConstant(matched, rep, &loc_cst)) {
      if (base::bits::IsPowerOfTwo(loc_cst)) {
        *ret_cst = loc_cst;
        return true;
      }
    }
    return false;
  }

  bool MatchPowerOfTwoWord32Constant(V<Any> matched, int32_t* divisor) const {
    int64_t cst;
    if (MatchPowerOfTwoWordConstant(matched, &cst,
                                    WordRepresentation::Word32())) {
      DCHECK_LE(cst, std::numeric_limits<int32_t>().max());
      *divisor = static_cast<int32_t>(cst);
      return true;
    }
    return false;
  }

 private:
  const Graph& graph_;
};

template <typename T, bool HasConstexpr>
bool detail::ValueMatch<T, HasConstexpr>::matches(
    OpIndex matched, const OperationMatcher* matcher) {
  switch (v_.index()) {
    case 0:
      return true;
    case 1:
      return std::get<1>(v_) == matched;
    case 2:
      return true;
    case 3: {
      const ConstantOp* c = matcher->template TryCast<ConstantOp>(matched);
      if (!c) return false;
      if (c->rep != v_traits<T>::rep) return false;
      // TODO: Need to fix this for handles and such...
      return c->storage.integral ==
             (ConstantOp::Storage{static_cast<uint64_t>(std::get<3>(v_))}
                  .integral);
    }
  }
  // unreachable
  return false;
}

template <typename T, bool HasConstexpr>
void detail::ValueMatch<T, HasConstexpr>::bind(
    OpIndex matched, const OperationMatcher* matcher) {
  DCHECK(matches(matched, matcher));
  if (v_.index() == 2) *std::get<2>(v_) = matched;
}

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_OPERATION_MATCHER_H_
