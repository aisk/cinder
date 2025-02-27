// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include <iosfwd>
#include <set>
#include <string>
#include <unordered_map>

#include "Jit/log.h"
#include "Jit/util.h"

namespace jit {
namespace codegen {

// A physical location (register or stack slot). If this represents a stack
// slot (is_memory() is true) then `loc` is relative to RBP.
struct PhyLocation {
  PhyLocation() : loc(REG_INVALID) {}
  constexpr PhyLocation(int l) : loc(l) {}
  constexpr operator int() const {
    return loc;
  }
  bool is_memory() const {
    return loc < 0;
  }
  bool is_register() const {
    return loc >= 0;
  }
  bool is_gp_register() const {
    return is_register() && loc < XMM_REG_BASE;
  }
  bool is_fp_register() const {
    return is_register() && loc >= XMM_REG_BASE;
  }

  int loc;

#define FOREACH_GP(X) \
  X(RAX)              \
  X(RCX)              \
  X(RDX)              \
  X(RBX)              \
  X(RSP)              \
  X(RBP)              \
  X(RSI)              \
  X(RDI)              \
  X(R8)               \
  X(R9)               \
  X(R10)              \
  X(R11)              \
  X(R12)              \
  X(R13)              \
  X(R14)              \
  X(R15)

#define FOREACH_XMM(X) \
  X(XMM0)              \
  X(XMM1)              \
  X(XMM2)              \
  X(XMM3)              \
  X(XMM4)              \
  X(XMM5)              \
  X(XMM6)              \
  X(XMM7)              \
  X(XMM8)              \
  X(XMM9)              \
  X(XMM10)             \
  X(XMM11)             \
  X(XMM12)             \
  X(XMM13)             \
  X(XMM14)             \
  X(XMM15)

  enum Reg : int {
    REG_INVALID = -1,
#define DECLARE_REG(v, ...) v,
    FOREACH_GP(DECLARE_REG) FOREACH_XMM(DECLARE_REG)
#undef DECLARE_REG
  };

#define COUNT_REGS(...) +1
  static constexpr int NUM_GP_REGS = FOREACH_GP(COUNT_REGS);
  static constexpr int XMM_REG_BASE = XMM0;
  static constexpr int NUM_XMM_REGS = FOREACH_XMM(COUNT_REGS);
  static constexpr int NUM_REGS = NUM_GP_REGS + NUM_XMM_REGS;
#undef COUNT_REGS

  bool operator==(const PhyLocation& rhs) const {
    return loc == rhs.loc;
  }
  bool operator==(int rhs) const {
    return loc == rhs;
  }

  bool operator!=(const PhyLocation& rhs) const {
    return loc != rhs.loc;
  }
  bool operator!=(int rhs) const {
    return loc != rhs;
  }

  // Parses the register name in string and returns the corresponding
  // physical register.
  // Returns REG_INVALID if name is not a valid register name.
  static PhyLocation parse(const std::string& name);
};

class PhyRegisterSet {
 public:
  constexpr PhyRegisterSet() : rs_(0) {}
  constexpr PhyRegisterSet(PhyLocation r) : rs_(0) {
    rs_ |= (1 << r);
  }

  constexpr PhyRegisterSet operator|(PhyLocation reg) const {
    PhyRegisterSet set;
    set.rs_ = rs_ | (1 << reg);
    return set;
  }

  constexpr PhyRegisterSet operator|(const PhyRegisterSet& rs) const {
    PhyRegisterSet res;
    res.rs_ = rs_ | rs.rs_;
    return res;
  }

  PhyRegisterSet& operator|=(const PhyRegisterSet& rs) {
    rs_ |= rs.rs_;
    return *this;
  }

  constexpr PhyRegisterSet operator-(PhyLocation rs) const {
    return operator-(PhyRegisterSet(rs));
  }

  constexpr PhyRegisterSet operator-(PhyRegisterSet rs) const {
    PhyRegisterSet set;
    set.rs_ = rs_ & ~(rs.rs_);
    return set;
  }

  constexpr PhyRegisterSet operator&(PhyRegisterSet rs) const {
    PhyRegisterSet set;
    set.rs_ = rs_ & rs.rs_;
    return set;
  }

  constexpr bool operator==(const PhyRegisterSet& rs) const {
    return rs_ == rs.rs_;
  }

  constexpr bool Empty() const {
    return rs_ == 0;
  }
  int count() const {
    return popcount(rs_);
  }
  PhyLocation GetFirst() const {
    return __builtin_ctz(rs_);
  }
  constexpr void RemoveFirst() {
    rs_ &= (rs_ - 1);
  }

  void Set(PhyLocation reg) {
    rs_ |= (1 << reg);
  }
  void Reset(PhyLocation reg) {
    rs_ &= ~(1 << reg);
  }
  void ResetAll() {
    rs_ = 0;
  }

  bool Has(PhyLocation reg) const {
    return rs_ & (1 << reg);
  }

  constexpr int GetMask() const {
    return rs_;
  }

 private:
  unsigned rs_;
};

std::ostream& operator<<(std::ostream& out, const PhyLocation& loc);

#define ADD_REG(v, ...) | PhyLocation::v
static constexpr PhyRegisterSet ALL_GP_REGISTERS =
    PhyRegisterSet() FOREACH_GP(ADD_REG);
static constexpr PhyRegisterSet ALL_XMM_REGISTERS =
    PhyRegisterSet() FOREACH_XMM(ADD_REG);
static constexpr PhyRegisterSet ALL_REGISTERS =
    ALL_GP_REGISTERS | ALL_XMM_REGISTERS;
#undef ADD_REG

static constexpr PhyRegisterSet STACK_REGISTERS =
    PhyRegisterSet(PhyLocation::RSP) | PhyLocation::RBP;

static constexpr PhyRegisterSet INIT_REGISTERS =
    ALL_REGISTERS - STACK_REGISTERS;

static constexpr PhyRegisterSet CALLER_SAVE_REGS =
    PhyRegisterSet(PhyLocation::RAX) | PhyLocation::RCX | PhyLocation::RDX |
    PhyLocation::RSI | PhyLocation::RDI | PhyLocation::R8 | PhyLocation::R9 |
    PhyLocation::R10 | PhyLocation::R11 | ALL_XMM_REGISTERS;

static constexpr PhyRegisterSet CALLEE_SAVE_REGS =
    INIT_REGISTERS - CALLER_SAVE_REGS;

static constexpr PhyLocation ARGUMENT_REGS[] = {
    PhyLocation::RDI,
    PhyLocation::RSI,
    PhyLocation::RDX,
    PhyLocation::RCX,
    PhyLocation::R8,
    PhyLocation::R9};

static constexpr PhyLocation FP_ARGUMENT_REGS[] = {
    PhyLocation::XMM0,
    PhyLocation::XMM1,
    PhyLocation::XMM2,
    PhyLocation::XMM3,
    PhyLocation::XMM4,
    PhyLocation::XMM5,
    PhyLocation::XMM6,
    PhyLocation::XMM7};

} // namespace codegen
} // namespace jit

namespace std {
template <>
struct hash<jit::codegen::PhyLocation> {
  std::size_t operator()(jit::codegen::PhyLocation const& s) const noexcept {
    return s.loc;
  }
};
}; // namespace std
