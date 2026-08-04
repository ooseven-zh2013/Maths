#pragma once
#ifndef ALGEBRAIC_EXPRESSION_HPP
#define ALGEBRAIC_EXPRESSION_HPP
#include <algorithm>
#include <cctype>
#include <compare>
#include <map>
#include <string>
#include <vector>

#include "numbers.hpp"

class Name {
public:
  Name() {}
  Name(std::string_view name_) { *this = name_; }

  std::string str() const { return name; }

  Name &operator=(std::string_view name_) {
    if (check(name_)) {
      name = std::string(name_);
      return *this;
    }
    throw std::invalid_argument("Invalid name");
  }

  auto operator<=>(const Name &oth) const { return name <=> oth.name; }

private:
  std::string name;
  bool check(std::string_view name_) {
    if (name_.empty())
      return false;
    bool allAlpha = true;
    bool allDigits = true;
    for (char c : name_) {
      unsigned char uc = static_cast<unsigned char>(c);
      if (!std::isalpha(uc))
        allAlpha = false;
      if (!std::isdigit(uc))
        allDigits = false;
      if (!allAlpha && !allDigits)
        return false;
    }
    return allAlpha || allDigits;
  }
};

class Variable {
public:
  Variable(std::string_view name_) { convertToName(name_, false); }
  Variable(std::string_view name_, bool allowNumeric) { convertToName(name_, allowNumeric); }

  Variable &operator=(std::string_view name_) {
    convertToName(name_, false);
    return *this;
  }

  bool hasIndex() const { return !index.empty(); }

  std::string str() const {
    std::string res = name.str();
    if (hasIndex()) {
      res += '_';
      if (index.size() == 1) {
        if (index.begin()->hasIndex()) {
          res += '{';
          res += index.begin()->str();
          res += '}';
        } else {
          res += index.begin()->str();
        }
      } else {
        res += '{';
        bool first = true;
        for (const Variable &idx : index) {
          if (first) {
            first = false;
          } else {
            res += ',';
          }
          res += idx.str();
        }
        res += '}';
      }
    }
    return res;
  }

  auto operator<=>(const Variable &oth) const {
    if (auto cmp = name <=> oth.name; cmp != 0)
      return cmp;
    for (size_t i = 0, size_ = std::min(index.size(), oth.index.size()); i < size_; ++i) {
      if (auto cmp = index[i] <=> oth.index[i]; cmp != 0)
        return cmp;
    }
    return index.size() <=> oth.index.size();
  }

  bool operator==(const Variable &oth) const { return (*this <=> oth) == 0; }

private:
  Name name;                   // variable name
  std::vector<Variable> index; // variable index
  void convertToName(std::string_view name_, bool allowNumeric) {
    // convert LaTex expression to NameType
    Name tmpName;
    std::vector<Variable> tmpIdx;
    auto pos = name_.find('_');
    if (pos == std::string_view::npos) {
      // 不允许纯数字作为变量名（纯数字应作为常数或索引），除非 allowNumeric 为 true
      bool allDigits = !name_.empty() && std::all_of(name_.begin(), name_.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c));
      });
      if (allDigits && !allowNumeric) {
        throw std::invalid_argument("Invalid variable name");
      }
      tmpName = name_;
    } else {
      // 基名必须为字母（不能是纯数字或包含其它字符）
      auto base = name_.substr(0, pos);
      if (!std::all_of(base.begin(), base.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)); })) {
        throw std::invalid_argument("Invalid variable base name");
      }
      tmpName = base;
      name_ = name_.substr(pos + 1);

      if (name_.empty()) {
        throw std::invalid_argument("Invalid variable name");
      }

      if (name_.front() != '{') {
        // 单索引形式，例如 a_1 或 a_x
        // 遵循 LaTeX 规范：_ 后不带 {} 时只接受单个字符（字母或数字）
        if (name_.size() != 1) {
          throw std::invalid_argument("Invalid variable index: unbraced index must be a single character");
        }
        if (name_.find('_') != std::string_view::npos) {
          throw std::invalid_argument("Invalid variable index");
        }
        tmpIdx.emplace_back(name_, true);
      } else {
        // 大括号形式 a_{...}
        if (name_.back() != '}') {
          throw std::invalid_argument("Invalid variable name");
        }
        name_ = name_.substr(1, name_.size() - 2);
        size_t last = 0;
        while (true) {
          pos = name_.find(',', last);
          if (pos == std::string_view::npos) {
            tmpIdx.emplace_back(name_.substr(last), true);
            break;
          } else {
            tmpIdx.emplace_back(name_.substr(last, pos - last), true);
            last = pos + 1;
          }
        }
      }
    }
    name = std::move(tmpName);
    index = std::move(tmpIdx);
  }
};

// incomplete

class Monomial {
public:
  Monomial() = default;
  // Monomial(std::string_view sv) {}

private:
  Fraction coeff;
  std::vector<std::pair<Variable, unsigned long long>> vars;
};

class Polynomial {
public:
private:
  std::map<std::pair<std::vector<Variable>, unsigned long long>, Fraction> terms;
};

#endif // ALGEBRAIC_EXPRESSION_HPP
