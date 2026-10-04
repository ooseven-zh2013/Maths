export module maths.algebra:region_multi;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import :expression;
import :scope;
import :constraint_system;
import :domain_multi;

// ==================== 多元区域 ====================
//
// 一元的定义域是 `RealSet`：区间 + 有限点集。**那个表示在多维不成立** ——
// `{x ≥ 0}`、`{x ≥ 0 ∧ y ≥ 0}`、`{x² + y² ≤ 1}` 都不是「一维区间的组合」。
//
// 多元的对应物是**半代数集**，本库用它的**析取范式（DNF）**：
//
//     区域 = 若干「合取支」的并
//     合取支 = ConstraintSystem（若干 `多项式 + 关系` 的合取）
//
// 为什么不做更复杂的表示：DNF 的交并补都是机械的（分配律 / 德摩根），
// 而 CAD 那条路能给出「区域」但代价是研究级算法。一维那边 `RealSet` 能做的事
// 这里靠「DNF + 逐支判空」也能做，只是支数会涨。
//
// **涨多少**：交两个各 n 支的区域 → 最多 n·m 支。反复交会在几轮之后失控，
// 所以本库不做区域简化（合并同形支、吸收冗余支）。调用方要节制用。

export namespace maths {

class Region {
public:
  // 空 vector 表示**整空间**（没有约束 = 什么都满足）
  Region() = default;
  explicit Region(std::vector<ConstraintSystem> branches) : branches_(std::move(branches)) {}

  static Result<Region> wholeSpace() { return Result<Region>(Region()); }

  static Result<Region> fromSystem(ConstraintSystem system) { return Result<Region>(Region({std::move(system)})); }

  const std::vector<ConstraintSystem> &branches() const { return branches_; }
  bool isWholeSpace() const { return branches_.empty(); }
  // 整空间、或有一支的约束为空 → 整空间
  bool isTrivial() const;

  // ==================== 集合运算 ====================

  // 交：分配律展开 (A₁ ∪ A₂) ∩ (B₁ ∪ B₂) = (A₁∩B₁) ∪ (A₁∩B₂) ∪ (A₂∩B₁) ∪ (A₂∩B₂)
  Result<Region> intersect(const Region &rhs) const {
    if (isWholeSpace()) {
      return Result<Region>(rhs);
    }
    if (rhs.isWholeSpace()) {
      return Result<Region>(*this);
    }
    std::vector<ConstraintSystem> combined;
    for (const ConstraintSystem &left : branches_) {
      for (const ConstraintSystem &right : rhs.branches_) {
        const Result<ConstraintSystem> narrowed = left.andWith(right);
        if (narrowed.isErr()) {
          return std::unexpected(narrowed.unwrapErr());
        }
        combined.push_back(narrowed.unwrap());
      }
    }
    return Result<Region>(Region(std::move(combined)));
  }

  // 并：直接拼接（析取就是拼）
  Result<Region> unite(const Region &rhs) const {
    if (isWholeSpace() || rhs.isWholeSpace()) {
      return Result<Region>(Region());
    }
    std::vector<ConstraintSystem> combined = branches_;
    const std::vector<ConstraintSystem> &others = rhs.branches_;
    combined.insert(combined.end(), others.begin(), others.end());
    return Result<Region>(Region(std::move(combined)));
  }

  // 整空间、或有一支的约束为空 → 整空间

  // 补：德摩根 ¬(A₁ ∧ … ∧ Aₖ) = ¬A₁ ∨ … ∨ ¬Aₖ，而 ¬(P ≥ 0) = P < 0 那一类
  Result<Region> complement() const {
    if (isWholeSpace()) {
      return Result<Region>::err(MathsError::InvalidRange); // 空集不是 DNF 能表示的（支不能为空）
    }
    // 两层德摩根，顺序不能颠倒：
    //   支内部（一支是**合取**）：¬(B₁ ∧ … ∧ Bₗ) = ¬B₁ ∨ … ∨ ¬Bₗ   → 拆成一支的析取
    //   支与支之间（DNF 是**析取**）：¬(A₁ ∨ … ∨ Aₖ) = ¬A₁ ∧ … ∧ ¬Aₖ → 各支的补再**互交**
    //
    // 所以：先把每支内部 negate 成一组单原子支（那一组是并），再把「每支的那一组」互交。
    // 少 either 一步都错：在支内部直接交 → 得到 ¬(A∧B) 的反面；把拆出来的支直接拼 →
    // 得到 ¬(A₁∨A₂) 的反面。
    Region result; // 整空间是交的单位元
    for (const ConstraintSystem &branch : branches_) {
      if (branch.atoms().empty()) {
        return Result<Region>::err(MathsError::InvalidRange); // 原来那支是整空间 → 补是空集
      }
      std::vector<ConstraintSystem> flipped; // 这一支的补 = 这些的并
      for (const AtomConstraint &atom : branch.atoms()) {
        flipped.push_back(ConstraintSystem({AtomConstraint(atom.expression(), opposite(atom.relation()))}));
      }
      const Result<Region> narrowed = result.intersect(Region(std::move(flipped)));
      if (narrowed.isErr()) {
        return narrowed;
      }
      result = narrowed.unwrap();
    }
    return Result<Region>(result);
  }

  // ==================== 判定 ====================

  // 空吗：逐支判空（`isEmpty(ConstraintSystem)` 判不了时会报 DomainNotDecidable）
  Result<bool> isEmptyRegion() const;

  // 这个点在区域里吗：任一支满足即可
  Result<bool> admits(const Scope &point) const {
    if (isWholeSpace()) {
      return Result<bool>(true);
    }
    for (const ConstraintSystem &branch : branches_) {
      const Result<bool> inside = branch.admits(point);
      if (inside.isErr()) {
        return std::unexpected(inside.unwrapErr());
      }
      if (inside.unwrap()) {
        return Result<bool>(true);
      }
    }
    return Result<bool>(false);
  }

  std::string str() const { return render(false); }
  std::string latex() const { return render(true); }

private:
  // 关系的相反：`≥` ↔ `<`，`≠` ↔ `=`，其余同理
  static Relation opposite(Relation relation) {
    switch (relation) {
    case Relation::Greater:
      return Relation::LessEqual;
    case Relation::GreaterEqual:
      return Relation::Less;
    case Relation::Less:
      return Relation::GreaterEqual;
    case Relation::LessEqual:
      return Relation::Greater;
    case Relation::Equal:
      return Relation::NotEqual;
    case Relation::NotEqual:
      return Relation::Equal;
    }
    return relation;
  }

  std::string render(bool useLatex) const {
    if (isWholeSpace()) {
      return useLatex ? "\\mathbb{R}^{n}" : "R^n";
    }
    std::string result;
    for (std::size_t index = 0; index < branches_.size(); ++index) {
      if (index > 0) {
        result += useLatex ? " \\cup " : " or ";
      }
      result += branches_[index].latex();
    }
    return result;
  }

  std::vector<ConstraintSystem> branches_;
};

// 整空间、或有一支的约束为空 → 整空间
inline bool Region::isTrivial() const {
  if (isWholeSpace()) {
    return true;
  }
  return std::any_of(branches_.begin(), branches_.end(),
                     [](const ConstraintSystem &branch) { return branch.isTrivial(); });
}

// 空吗：逐支判空
inline Result<bool> Region::isEmptyRegion() const {
  if (isWholeSpace()) {
    return Result<bool>(false);
  }
  for (const ConstraintSystem &branch : branches_) {
    const Result<bool> empty = isEmpty(branch);
    if (empty.isErr()) {
      return empty;
    }
    if (!empty.unwrap()) {
      return Result<bool>(false); // 有一支非空 → 整体非空
    }
  }
  return Result<bool>(true);
}

} // namespace maths
