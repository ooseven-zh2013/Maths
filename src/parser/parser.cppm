export module maths.parser;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebra;
import maths.algebraic_number;
import maths.real_set;

export namespace maths {

// 简易表达式解析器：把文本解析成 RationalFunction。
//
// 支持的语法：
//   expr    := term (('+' | '-') term)*
//   term    := power (('*' | '/') power)*
//   power   := primary ('^' 非负整数)?     ← 幂从 primary 起，一元负号在外层
//   unary   := ('-' | '+')? power         ← 所以 -x^2 = -(x^2)，不是 (-x)^2
//   primary := 整数 | 变量名 | '(' expr ')'
//
// 结果统一用 RationalFunction 承载：除法必然引入分式，用统一类型可以省掉
// 「多项式还是分式」的分支判断。
//
// 另外提供 parseAssignment：解析代入条件 "变量 = 表达式"。
// 支持两侧交换（C = x 等价于 x = C）；常数恒等式（C = C）返回 nullopt 表示无需记录。
// 刻意不支持需要解方程的形式（如 x + 1 = 2），这类输入会明确报错而不是猜。
//
// 另有 parseErase：识别「解除绑定」的输入 x = x —— 意思是删掉该变量此前记录的约束。
// 删除与赋值是两种不同的动作，所以单列一个函数而不是塞进 Assignment；
// 调用方应先试 parseErase，落空再走 parseAssignment。

namespace expression_detail {

// 非负整数的严格解析：整串必须都是数字。
//
// std::stoull 遇到非法字符**只解析前缀且不抛异常**（"1/2" 直接返回 1），
// 所以 try/catch 拦不住 —— `2^{1/2}` 会被读成指数 1，静默得到 2。
// 凡是把文本交给 stoull 的地方都先过这里。
inline std::optional<unsigned long long> parseWholeUnsigned(std::string_view text) {
  std::string digits;
  for (const char character : text) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
      return std::nullopt;
    }
    digits += character;
  }
  if (digits.empty() || digits.size() > 19) {
    return std::nullopt;
  }
  try {
    return static_cast<unsigned long long>(std::stoull(digits));
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

// 系数开 n 次根。只有能表示无理数的系数类型（如实代数数）才可用；
// 有理系数域 ℚ 里没有根式，于是 \sqrt{…} 在有理解析这条路上会明确报错 ——
// 因此含根号的式子在有理解析这条路上必然失败，要靠按代数系数实例化的那条路接手。
template <class Coefficient> inline Result<Coefficient> coefficientNthRoot(const Coefficient &value, unsigned degree) {
  if constexpr (requires(const Coefficient &base, unsigned power) { base.nthRoot(power); }) {
    return value.nthRoot(degree);
  } else {
    (void)value;
    (void)degree;
    return std::unexpected(MathsError::InvalidExpression);
  }
}

// 读取一个花括号分组（允许前面有空白），position 停在 } 之后
inline bool takeBracedGroup(std::string_view source, std::size_t &position, std::string &out) {
  while (position < source.size() && std::isspace(static_cast<unsigned char>(source[position])) != 0) {
    ++position;
  }
  if (position >= source.size() || source[position] != '{') {
    return false;
  }

  int depth = 0;
  const std::size_t start = position + 1;
  while (position < source.size()) {
    if (source[position] == '{') {
      ++depth;
    } else if (source[position] == '}') {
      --depth;
      if (depth == 0) {
        out = std::string(source.substr(start, position - start));
        ++position;
        return true;
      }
    }
    ++position;
  }
  return false;
}

// 把 LaTeX 写法规范化成解析器能直接处理的普通写法：
//   \frac{a}{b} → (a)/(b)   （支持嵌套）
//   \cdot、\times → *      \div → /
//   x^{2} → x^2
//   \left、\right → 忽略
// 其余字符原样保留。这样解析器本身只需处理一种语法。
inline std::string normalizeLatex(std::string_view source) {
  std::string result;
  result.reserve(source.size());
  std::size_t position = 0;

  while (position < source.size()) {
    if (source.compare(position, 5, "\\frac") == 0) {
      position += 5;
      std::string numerator;
      std::string denominator;
      if (!takeBracedGroup(source, position, numerator) || !takeBracedGroup(source, position, denominator)) {
        result += "\\frac"; // 结构不完整，原样保留，交给解析器报错
        continue;
      }
      result += '(';
      result += normalizeLatex(numerator);
      result += ")/(";
      result += normalizeLatex(denominator);
      result += ')';
      continue;
    }
    if (source.compare(position, 5, "\\cdot") == 0) {
      result += '*';
      position += 5;
      continue;
    }
    if (source.compare(position, 6, "\\times") == 0) {
      result += '*';
      position += 6;
      continue;
    }
    if (source.compare(position, 4, "\\div") == 0) {
      result += '/';
      position += 4;
      continue;
    }
    if (source.compare(position, 5, "\\left") == 0) {
      position += 5;
      continue;
    }
    if (source.compare(position, 6, "\\right") == 0) {
      position += 6;
      continue;
    }
    if (source.compare(position, 2, "^{") == 0) { // "^{" 只有 2 个字符
      // 指数只接受整数，因此把花括号展开 —— 但**只有确实是整数时才展开**。
      // 早先这里无条件剥括号，于是 x^{1/2} 被改写成 x^1/2，
      // 进而被解析成 (x^1)/2 并当成合法结果返回：不报错、给错答案，最坏的一种失败。
      // 内容不是整数就原样保留（连同 '{'），由指数解析器明确报错。
      const std::size_t close = source.find('}', position + 2);
      if (close != std::string_view::npos) {
        const std::string_view body = source.substr(position + 2, close - position - 2);
        const bool isUnsignedInteger = !body.empty() && std::all_of(body.begin(), body.end(), [](char character) {
          return std::isdigit(static_cast<unsigned char>(character)) != 0;
        });
        if (isUnsignedInteger) {
          result += '^';
          result.append(body);
          position = close + 1;
          continue;
        }
      }
    }
    result += source[position];
    ++position;
  }
  return result;
}

template <class Coefficient> class ParserOf {
public:
  explicit ParserOf(std::string_view source) : text(source) {}

  Result<RationalFunctionOf<Coefficient>> parse() {
    skipSpaces();
    if (atEnd()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    Result<RationalFunctionOf<Coefficient>> value = parseAdditive();
    if (value.isErr()) {
      return value;
    }
    skipSpaces();
    if (!atEnd()) {
      return std::unexpected(MathsError::InvalidExpression); // 存在无法消费的残留字符
    }
    return value;
  }

private:
  bool atEnd() const { return position >= text.size(); }
  char peek() const { return atEnd() ? '\0' : text[position]; }
  bool isDigit() const { return !atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0; }
  bool isAlpha() const { return !atEnd() && std::isalpha(static_cast<unsigned char>(peek())) != 0; }

  void skipSpaces() {
    while (!atEnd() && std::isspace(static_cast<unsigned char>(text[position])) != 0) {
      ++position;
    }
  }

  Result<RationalFunctionOf<Coefficient>> parseAdditive() {
    Result<RationalFunctionOf<Coefficient>> left = parseMultiplicative();
    if (left.isErr()) {
      return left;
    }
    while (true) {
      skipSpaces();
      const char operation = peek();
      if (operation != '+' && operation != '-') {
        return left;
      }
      ++position;
      Result<RationalFunctionOf<Coefficient>> right = parseMultiplicative();
      if (right.isErr()) {
        return right;
      }
      left = (operation == '+') ? left.unwrap() + right.unwrap() : left.unwrap() - right.unwrap();
    }
  }

  // 下一个位置能否开始一个因子 —— 用于识别隐含乘法
  bool startsPrimary() const {
    if (atEnd()) {
      return false;
    }
    const char character = peek();
    return std::isalpha(static_cast<unsigned char>(character)) != 0 ||
           std::isdigit(static_cast<unsigned char>(character)) != 0 || character == '(' || character == '{' ||
           character == '\\';
  }

  Result<RationalFunctionOf<Coefficient>> parseMultiplicative() {
    Result<RationalFunctionOf<Coefficient>> left = parseUnary();
    if (left.isErr()) {
      return left;
    }
    while (true) {
      skipSpaces();
      const char operation = peek();

      // 隐含乘法：数学书写里 xy 就是 x*y，2x 就是 2*x。
      // 没有这一条的话，xy 会被 parseVariable 吞成一个变量名。
      if (operation != '*' && operation != '/' && !startsPrimary()) {
        return left;
      }
      if (operation == '*' || operation == '/') {
        ++position;
      }

      Result<RationalFunctionOf<Coefficient>> right = parseUnary();
      if (right.isErr()) {
        return right;
      }
      if (operation == '/') {
        Result<RationalFunctionOf<Coefficient>> quotient = left.unwrap() / right.unwrap();
        if (quotient.isErr()) {
          return quotient;
        }
        left = quotient;
        continue;
      }
      left = left.unwrap() * right.unwrap();
    }
  }

  // 幂比一元负号**紧**：`-x^2` 是 `-(x^2)`，不是 `(-x)^2`（与 C / Python 一致）。
  // 所以一元负号在外、幂在内；反过来（power 先调 unary）会把 `-x^2` 算成 `x^2` ——
  // 那个负号被 `^` 吃掉了。
  //
  // 只吃**一个** `^`：`x^2^3` 仍然报「无法解析的残留」，不悄悄按左结合算成 (x²)³。
  Result<RationalFunctionOf<Coefficient>> parsePower() {
    Result<RationalFunctionOf<Coefficient>> base = parsePrimary();
    if (base.isErr()) {
      return base;
    }
    skipSpaces();
    if (peek() != '^') {
      return base;
    }
    ++position;
    Result<unsigned long long> exponent = parseExponent();
    if (exponent.isErr()) {
      return std::unexpected(exponent.unwrapErr());
    }
    return powerOf(base.unwrap(), exponent.unwrap());
  }

  Result<RationalFunctionOf<Coefficient>> parseUnary() {
    skipSpaces();
    if (peek() == '-') {
      ++position;
      Result<RationalFunctionOf<Coefficient>> operand = parseUnary();
      if (operand.isErr()) {
        return operand;
      }
      return -operand.unwrap();
    }
    if (peek() == '+') {
      ++position;
      return parseUnary();
    }
    return parsePower();
  }

  Result<RationalFunctionOf<Coefficient>> parsePrimary() {
    skipSpaces();
    if (atEnd()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    if (peek() == '(') {
      ++position;
      Result<RationalFunctionOf<Coefficient>> inner = parseAdditive();
      if (inner.isErr()) {
        return inner;
      }
      skipSpaces();
      if (peek() != ')') {
        return std::unexpected(MathsError::InvalidExpression);
      }
      ++position;
      return inner;
    }
    // \sqrt{…} / \sqrt[n]{…}：只支持**不含变量**的被开方数（根号包的是数）。
    // 根号包变量（\sqrt{x}）需要代数函数域 ℚ(x)[y]/(y²−x)，本库没有这个表示，
    // 会在 parseRadical 里明确报错，而不是悄悄算错。
    if (text.compare(position, 5, "\\sqrt") == 0) {
      return parseRadical();
    }
    if (peek() == '{') {
      return parseBracedVariable();
    }
    if (isDigit()) {
      return parseNumber();
    }
    if (isAlpha()) {
      return parseVariable();
    }
    return std::unexpected(MathsError::InvalidExpression);
  }

  Result<RationalFunctionOf<Coefficient>> parseRadical() {
    position += 5; // 吃掉 "\sqrt"
    skipSpaces();

    unsigned degree = 2;
    if (!atEnd() && peek() == '[') {
      ++position;
      const std::size_t start = position;
      while (!atEnd() && peek() != ']') {
        ++position;
      }
      if (atEnd()) {
        return std::unexpected(MathsError::InvalidExpression); // 少了 ']'
      }
      const std::string_view degreeText = text.substr(start, position - start);
      ++position;
      const std::optional<unsigned long long> parsed = parseWholeUnsigned(degreeText);
      if (!parsed || *parsed == 0 || *parsed > static_cast<unsigned long long>(std::numeric_limits<unsigned>::max())) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      degree = static_cast<unsigned>(*parsed);
    }

    std::string radicandText;
    if (!takeBracedGroup(text, position, radicandText)) {
      return std::unexpected(MathsError::InvalidExpression); // 根号下必须是花括号分组
    }

    // 被开方数按同一套语法递归解析，然后要求它**不含变量**
    Result<RationalFunctionOf<Coefficient>> radicand = ParserOf<Coefficient>(radicandText).parse();
    if (radicand.isErr()) {
      return radicand;
    }
    Result<MonomialOf<Coefficient>> numerator = radicand.unwrap().getNumerator().toMonomial();
    Result<MonomialOf<Coefficient>> denominator = radicand.unwrap().getDenominator().toMonomial();
    if (numerator.isErr() || !numerator.unwrap().isConstant() || denominator.isErr() ||
        !denominator.unwrap().isConstant()) {
      return std::unexpected(MathsError::InvalidExpression); // \sqrt{x} 这类：本库表示不了
    }

    Result<Coefficient> value = numerator.unwrap().getCoefficient() / denominator.unwrap().getCoefficient();
    if (value.isErr()) {
      return std::unexpected(value.unwrapErr());
    }
    Result<Coefficient> root = coefficientNthRoot(value.unwrap(), degree);
    if (root.isErr()) {
      return std::unexpected(root.unwrapErr());
    }
    return RationalFunctionOf<Coefficient>(root.unwrap());
  }

  // {name} 一次性声明多字母变量名，可跟下标：{node}_{car}。
  // 不用花括号的话，连续的字母按隐含乘法拆开（node 即 n*o*d*e）。
  Result<RationalFunctionOf<Coefficient>> parseBracedVariable() {
    std::string name;
    if (!takeBracedGroup(text, position, name) || name.empty()) {
      return std::unexpected(MathsError::InvalidExpression);
    }

    if (!atEnd() && peek() == '_') {
      ++position;
      if (!atEnd() && peek() == '{') {
        std::string index;
        if (!takeBracedGroup(text, position, index)) {
          return std::unexpected(MathsError::InvalidExpression);
        }
        name += "_{" + index + "}";
      } else if (!atEnd()) {
        name += '_';
        name += peek();
        ++position;
      }
    }

    try {
      const Variable variable(name);
      return RationalFunctionOf<Coefficient>(
          MonomialOf<Coefficient>(detail::coefficientOne<Coefficient>(), {{variable, 1ULL}}));
    } catch (const MathsException &error) {
      return std::unexpected(error.code());
    }
  }

  Result<RationalFunctionOf<Coefficient>> parseNumber() {
    const std::size_t start = position;
    while (isDigit()) {
      ++position;
    }
    try {
      const long long value = std::stoll(std::string(text.substr(start, position - start)));
      return RationalFunctionOf<Coefficient>(MonomialOf<Coefficient>(Coefficient(Fraction(value, 1LL))));
    } catch (const std::exception &) {
      return std::unexpected(MathsError::InvalidExpression);
    }
  }

  Result<RationalFunctionOf<Coefficient>> parseVariable() {
    // 变量名 = 单个字母 + 可选下标（x、a_1、x_{i,j}）。
    // 连续字母不合并成一个名字，而是留给 parseMultiplicative 做隐含乘法：xy 即 x*y。
    // 这样「输入 xy」与「输入 x*y」得到同一个式子，也与 latex() 的输出闭环。
    const std::size_t start = position;
    ++position; // 调用方已确认首字符是字母

    if (!atEnd() && peek() == '_') {
      ++position;
      if (!atEnd() && peek() == '{') {
        int depth = 0;
        while (!atEnd()) {
          if (peek() == '{') {
            ++depth;
          } else if (peek() == '}') {
            --depth;
            if (depth == 0) {
              ++position;
              break;
            }
          }
          ++position;
        }
      } else if (!atEnd()) {
        ++position; // 单字符下标
      }
    }

    const std::string name(text.substr(start, position - start));
    try {
      const Variable variable(name);
      return RationalFunctionOf<Coefficient>(
          MonomialOf<Coefficient>(detail::coefficientOne<Coefficient>(), {{variable, 1ULL}}));
    } catch (const MathsException &error) {
      return std::unexpected(error.code());
    }
  }

  // 幂指数：接受 ^12 与 LaTeX 的 ^{12}。花括号形式必须单独校验，
  // 因为「读到数字就收工」会把 x^{1/2} 解析成 x^1 / 2 —— 静默给出错答案。
  Result<unsigned long long> parseExponent() {
    skipSpaces();
    if (peek() == '{') {
      std::string group;
      if (!takeBracedGroup(text, position, group)) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      std::string digits;
      for (const char character : group) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
          continue;
        }
        if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
          return std::unexpected(MathsError::InvalidExpression); // 例如 ^{1/2}、^{-1}
        }
        digits += character;
      }
      const std::optional<unsigned long long> parsed = expression_detail::parseWholeUnsigned(digits);
      if (!parsed) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      return *parsed;
    }
    return parseUnsignedInteger();
  }

  Result<unsigned long long> parseUnsignedInteger() {
    skipSpaces();
    if (!isDigit()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    const std::size_t start = position;
    while (isDigit()) {
      ++position;
    }
    const std::optional<unsigned long long> parsed =
        expression_detail::parseWholeUnsigned(text.substr(start, position - start));
    if (!parsed) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    return *parsed;
  }

  // 幂用重复乘法实现：RationalFunctionOf<Coefficient> 未提供 pow
  static Result<RationalFunctionOf<Coefficient>> powerOf(const RationalFunctionOf<Coefficient> &base,
                                                         unsigned long long exponent) {
    RationalFunctionOf<Coefficient> result(detail::coefficientOne<Coefficient>());
    for (unsigned long long i = 0; i < exponent; ++i) {
      result = result * base;
    }
    return result;
  }

  std::string_view text;
  std::size_t position{0};
};

// 是否为「恰好等于某个变量」的表达式，要求系数为 1、指数为 1、且分母为 1
inline std::optional<Variable> asSingleVariable(const RationalFunction &value) {
  Result<Monomial> denominator = value.getDenominator().toMonomial();
  if (denominator.isErr() || !denominator.unwrap().isConstant() || denominator.unwrap().getCoefficient() != 1LL) {
    return std::nullopt;
  }

  Result<Monomial> numerator = value.getNumerator().toMonomial();
  if (numerator.isErr() || numerator.unwrap().getCoefficient() != 1LL) {
    return std::nullopt;
  }

  const VarPowers &factors = numerator.unwrap().getFactors();
  if (factors.size() != 1 || factors[0].second != 1) {
    return std::nullopt;
  }
  return factors[0].first;
}

// 是否为常数（空 Scope 下可求值即说明不含变量）
inline std::optional<Fraction> asConstant(const RationalFunction &value) {
  Result<Fraction> evaluated = value.evaluate(Scope());
  if (evaluated.isErr()) {
    return std::nullopt;
  }
  return evaluated.unwrap();
}

// 含**变量**根号的表达式求值器：\sqrt{…} 直接产出根式扩张的元素，其余部分整块交给
// 有理解析器。全程走 RadicalExtension 的运算（它会按需自动扩域），错误一路以 Result 传出。
//
// 与 ParserOf<Coefficient> 的区别：那个模板要求「系数运算不失败且返回值」
// （PolynomialOf 内部把 lhsCoeff * rhsCoeff 当值用），而根式扩张的运算**可能失败**
// （同一平方类、跨变量），所以这里不复用那套模板，改用专门的求值器 —— 一趟即可，
// 不需要先扫一遍建域。
class RadicalParser {
public:
  explicit RadicalParser(std::string_view source) : text_(source) {}

  Result<RadicalExtension> parse() {
    skipSpaces();
    if (atEnd()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    Result<RadicalExtension> value = parseAdditive();
    if (value.isErr()) {
      return value;
    }
    skipSpaces();
    if (!atEnd()) {
      return std::unexpected(MathsError::InvalidExpression); // 有消费不掉的残留
    }
    return value;
  }

private:
  bool atEnd() const { return position_ >= text_.size(); }
  char peek() const { return atEnd() ? '\0' : text_[position_]; }

  void skipSpaces() {
    while (!atEnd() && std::isspace(static_cast<unsigned char>(peek())) != 0) {
      ++position_;
    }
  }

  bool takeToken(std::string_view token) {
    skipSpaces();
    if (text_.compare(position_, token.size(), token) != 0) {
      return false;
    }
    position_ += token.size();
    return true;
  }

  bool takeChar(char expected) {
    skipSpaces();
    if (peek() != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  bool atRadicalKeyword() const { return text_.compare(position_, 5, "\\sqrt") == 0; }

  // 下一个位置能否开始一个因子（用于隐含乘法）
  bool startsAtom() const {
    if (atEnd()) {
      return false;
    }
    if (atRadicalKeyword()) {
      return true;
    }
    const char character = peek();
    return character != '+' && character != '-' && character != '*' && character != '/' && character != '(' &&
           character != ')' && character != '^';
  }

  Result<RadicalExtension> parseAdditive() {
    Result<RadicalExtension> left = parseMultiplicative();
    if (left.isErr()) {
      return left;
    }
    for (;;) {
      skipSpaces();
      const char operation = peek();
      if (operation != '+' && operation != '-') {
        return left;
      }
      ++position_;
      Result<RadicalExtension> right = parseMultiplicative();
      if (right.isErr()) {
        return right;
      }
      Result<RadicalExtension> combined =
          operation == '+' ? left.unwrap() + right.unwrap() : left.unwrap() - right.unwrap();
      if (combined.isErr()) {
        return combined;
      }
      left = combined;
    }
  }

  Result<RadicalExtension> parseMultiplicative() {
    Result<RadicalExtension> left = parseUnary();
    if (left.isErr()) {
      return left;
    }
    for (;;) {
      skipSpaces();
      char operation = peek();
      if (operation == '*' || operation == '/') {
        ++position_;
      } else if (startsAtom()) {
        operation = '*'; // 隐含乘法：2\sqrt{x}
      } else {
        return left;
      }
      Result<RadicalExtension> right = parseUnary();
      if (right.isErr()) {
        return right;
      }
      Result<RadicalExtension> combined =
          operation == '/' ? left.unwrap() / right.unwrap() : left.unwrap() * right.unwrap();
      if (combined.isErr()) {
        return combined;
      }
      left = combined;
    }
  }

  // 与通用解析器同一件事：一元负号在外、幂在内，`-x^2` 才是 -(x^2)
  Result<RadicalExtension> parsePower() {
    Result<RadicalExtension> base = parseAtom();
    if (base.isErr()) {
      return base;
    }
    if (!takeChar('^')) {
      return base;
    }
    Result<unsigned long long> exponent = parseExponent();
    if (exponent.isErr()) {
      return std::unexpected(exponent.unwrapErr());
    }
    RadicalExtension result(Fraction(1, 1));
    for (unsigned long long step = 0; step < exponent.unwrap(); ++step) {
      Result<RadicalExtension> next = result * base.unwrap();
      if (next.isErr()) {
        return next;
      }
      result = next.unwrap();
    }
    return result;
  }

  Result<unsigned long long> parseExponent() {
    skipSpaces();
    if (peek() == '{') {
      std::string group;
      if (!takeBracedGroup(text_, position_, group)) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      const std::optional<unsigned long long> parsed = parseWholeUnsigned(group);
      if (!parsed) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      return *parsed;
    }
    const std::size_t start = position_;
    while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
      ++position_;
    }
    const std::optional<unsigned long long> parsed = parseWholeUnsigned(text_.substr(start, position_ - start));
    if (!parsed) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    return *parsed;
  }

  Result<RadicalExtension> parseUnary() {
    skipSpaces();
    if (peek() == '-') {
      ++position_;
      Result<RadicalExtension> operand = parseUnary();
      if (operand.isErr()) {
        return operand;
      }
      return -operand.unwrap();
    }
    if (peek() == '+') {
      ++position_;
      return parseUnary();
    }
    return parsePower();
  }

  Result<RadicalExtension> parseAtom() {
    skipSpaces();
    if (atEnd()) {
      return std::unexpected(MathsError::InvalidExpression);
    }

    if (takeToken("\\sqrt")) {
      // 只支持二次根：高次根在含变量的情形下需要更大的结构
      skipSpaces();
      if (peek() == '[') {
        return std::unexpected(MathsError::InvalidExpression);
      }
      std::string radicandText;
      if (!takeBracedGroup(text_, position_, radicandText)) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      if (radicandText.find("\\sqrt") != std::string::npos) {
        return std::unexpected(MathsError::NestedRadical); // 根式套根式：代数函数域装不下
      }
      Result<RationalFunction> radicand = ParserOf<Fraction>(radicandText).parse();
      if (radicand.isErr()) {
        return std::unexpected(radicand.unwrapErr());
      }
      return RadicalExtension::make(radicand.unwrap());
    }

    if (peek() == '(') {
      ++position_;
      Result<RadicalExtension> inner = parseAdditive();
      if (inner.isErr()) {
        return inner;
      }
      skipSpaces();
      if (peek() != ')') {
        return std::unexpected(MathsError::InvalidExpression);
      }
      ++position_;
      return inner;
    }

    // 其余交给有理解析器：读一段「有理块」，直到顶层运算符、括号或下一个 \sqrt
    const std::string_view chunk = takeRationalChunk();
    if (chunk.find_first_not_of(" \t") == std::string_view::npos) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    Result<RationalFunction> parsed = ParserOf<Fraction>(chunk).parse();
    if (parsed.isErr()) {
      return std::unexpected(parsed.unwrapErr());
    }
    return RadicalExtension(parsed.unwrap()); // 零生成元元素：一个有理函数
  }

  // 有理块：连续字符直到顶层运算符、括号或下一个 \sqrt（`^` 留在块里，交给有理解析器）
  std::string_view takeRationalChunk() {
    const std::size_t start = position_;
    while (!atEnd()) {
      const char character = peek();
      if (character == '+' || character == '-' || character == '*' || character == '/' || character == '(' ||
          character == ')') {
        break;
      }
      if (atRadicalKeyword()) {
        break;
      }
      ++position_;
    }
    return text_.substr(start, position_ - start);
  }

  std::string_view text_;
  std::size_t position_{0};
};

} // namespace expression_detail

// 解析表达式：先做 LaTeX 规范化，再交给解析器。
// 失败返回 MathsError::InvalidExpression
inline Result<RationalFunction> parseExpression(std::string_view text) {
  // normalized 的生命周期覆盖整个 Parser 调用，Parser 持有的 string_view 不会悬垂
  const std::string normalized = expression_detail::normalizeLatex(text);
  return expression_detail::ParserOf<Fraction>(normalized).parse();
}

// 解析成**代数系数**的式子：根号可以出现在系数位置。
//
//   \sqrt{2}*x   → √2 · x
//   x + \sqrt{2} → x + √2
//   \sqrt[3]{2}*y
//
// 语法与 parseExpression 完全一致（同一个 ParserOf，只是系数换成实代数数），
// 区别只在于：有理系数域 ℚ 表示不了根式，所以那些写法在有理解析器里必然失败，
// 得走这条入口。\sqrt{x} 仍然不支持 —— 那是代数函数域（另一件事）。

// 解析含**变量**根号的表达式：\sqrt{x}、\sqrt{x^2+1}、2\sqrt{x}、\sqrt{x}\sqrt{x+1} …
//
// 返回一个根式扩张元素（`RadicalExtension`）：域按式子里出现的根号自动扩张，
// 于是「两个不同的根号」能自然地相加相乘。可代入求值（`evaluate`）。
//
// 边界：只支持**二次根**（`\sqrt{…}`），不支持 `\sqrt[n]{…}` 与嵌套根号；
// 根号下必须是含变量的有理函数（常数根号 `\sqrt{2}` 属于实代数数，请分开算）；
// 所有被开方数必须含同一个变量（系数的其它字母不受限）。
inline Result<RadicalExtension> parseRadicalExpression(std::string_view text) {
  const std::string normalized = expression_detail::normalizeLatex(text);
  return expression_detail::RadicalParser(normalized).parse();
}

inline Result<AlgebraicRationalFunction> parseAlgebraicExpression(std::string_view text) {
  const std::string normalized = expression_detail::normalizeLatex(text);
  return expression_detail::ParserOf<RealAlgebraicNumber>(normalized).parse();
}

// ---------------- 根式 → 分段函数 ----------------

namespace radical_branch_detail {

// 把 `|g|` 改写成 `\sqrt{(g)^2}`。
//
// 绝对值的**内部形式就是 √(g²)** —— 代数函数域装不下它（y² − g² 可约、y 是零因子），
// 所以统一先改写成根号，再交给下面那套「占位符 + 分支」的流程。
//
// ⚠️ g 外面要加**圆括号**而不是花括号：本库的花括号表示「多字母长变量名」
// （`{node}`），不是分组 —— `\sqrt{{x+1}^2}` 会被当成变量名 `x+1` 而解析失败。
//
// 竖线在本库的语法里没有别的用处（变量名只含字母数字下标），所以按成对扫即可；
// 落单的 `|` 原样留着，让解析器去报语法错。
inline std::string rewriteAbsoluteValues(std::string_view text) {
  std::string result;
  std::size_t cursor = 0;
  while (true) {
    const std::size_t open = text.find('|', cursor);
    if (open == std::string_view::npos) {
      result.append(text.substr(cursor));
      return result;
    }
    const std::size_t close = text.find('|', open + 1);
    if (close == std::string_view::npos) {
      result.append(text.substr(cursor));
      return result;
    }
    result.append(text.substr(cursor, open - cursor));
    result += "\\sqrt{(";
    result.append(text.substr(open + 1, close - open - 1));
    result += ")^2}";
    cursor = close + 1;
  }
}

// 一个「被开方数是完全平方」的根号：g² 里的 g，以及它在改写后的文本里占的那一位
struct SquareRadical {
  std::string placeholder; // 占位变量名（纯字母，不含花括号）
  RationalFunction root;   // g，满足 g² = 被开方数
};

// 有理函数是不是某个有理函数的平方；是就把那个 g 解出来。
//
// p/q 是平方 ⟺ p 与 q 各自都是多项式的平方 —— 分解到分子分母上各做一次
// （`√(g²) = |g|`、`√(g²/h²) = |g|/|h|`，两边一致，所以分开开方是对的）。
inline std::optional<RationalFunction> squareRootOf(const RationalFunction &value) {
  const std::set<Variable> variables = value.variables();
  if (variables.size() != 1) {
    return std::nullopt; // 多变量：分段函数是一元的，装不下
  }
  const Variable variable = *variables.begin();
  const std::optional<UnivariatePolynomial> numerator = toUnivariatePolynomial(value.getNumerator(), variable);
  const std::optional<UnivariatePolynomial> denominator = toUnivariatePolynomial(value.getDenominator(), variable);
  if (!numerator.has_value() || !denominator.has_value()) {
    return std::nullopt;
  }
  const std::optional<UnivariatePolynomial> numeratorRoot = numerator->squareRoot();
  const std::optional<UnivariatePolynomial> denominatorRoot = denominator->squareRoot();
  if (!numeratorRoot.has_value() || !denominatorRoot.has_value()) {
    return std::nullopt;
  }
  Result<RationalFunction> assembled = RationalFunction::make(fromUnivariatePolynomial(*numeratorRoot, variable),
                                                              fromUnivariatePolynomial(*denominatorRoot, variable));
  if (assembled.isErr()) {
    return std::nullopt;
  }
  return assembled.unwrap();
}

// 分支出数的上限：k 个完全平方的根号 → 2^k 支（与根式扩张的生成元上限同一个量级）
constexpr std::size_t kMaxSquareRadicals = 4;

} // namespace radical_branch_detail

// `√(x²)` 这类**被开方数是完全平方**的根号，在代数函数域里不是单值元素（它是 |x|），
// 所以 `parseRadicalExpression` 明确拒收。但它作为 **ℝ → ℝ 的函数**完全合法，
// 只是需要**分段**才装得下 —— 这一档就是把那种输入接下来：
//
//   √(x²)          →  { x on [0,+∞) ; −x on (−∞,0) }              （就是 |x|）
//   √(x²)·√(x²+1)  →  { x√(x²+1) on [0,+∞) ; −x√(x²+1) on (−∞,0) }
//
// 做法：把这些 `\sqrt{g²}` 逐个换成临时变量，用**现有的根式解析器**解析
// （此时没有完全平方的根号了，一定通过），再把临时变量代成 ±g 并按符号分成 2^k 支。
// 这样语法、优先级、报错全都与 `parseRadicalExpression` 一致，不必把解析器写第二遍。
//
// 只支持**一元**：分段函数这个类型本身就是一元的（多元的定义域是多维点集）。
//
// `|g|` 与 `√(g²)` 走的是**同一条路**（`|g|` 先被改写成 `\sqrt{{g}^2}`），
// 所以两者得到的是同一个分段函数，输出时再一起还原成 `|g|`。
// 解析结果：分段本身，外加「输入是不是写成 |g| / √(g²) 的」这个标记。
//
// 为什么必须把标记带出来：`|g|` 在 **g 恒非负** 时内部只剩一支
// （`|(x-1)^2/2|` 就是），而「一个本来就单支的分段」跟它结构上完全一样 ——
// 光看 `PiecewiseFunction` 分辨不出来。展示形态要一致，只能靠输入形态。
// 定义在文件后半段（它依赖塔），但下面那两档解析都要用到
inline Result<TowerExtension> parseTowerExpression(std::string_view text);

struct PiecewiseParseResult {
  PiecewiseFunction value;
  bool writtenAsAbsoluteValue{false};
};

namespace tower_parser_detail {
// 定义在下面那个命名空间里（它要用到塔），这里先声明 —— parsePiecewiseExpressionDetailed
// 夹在两处之间。
inline Result<PiecewiseParseResult> parseAbsoluteValueOverTower(std::string_view text);
} // namespace tower_parser_detail

inline Result<PiecewiseParseResult> parsePiecewiseExpressionDetailed(std::string_view text) {
  // 整条式子就是一个「内部带根号的绝对值」时走塔那一档（`|x-2\sqrt{x}|` 这类）。
  // 形状不匹配 → InvalidExpression，继续走下面的 `√(g²)` 改写路线；
  // 形状对但判不了符号 → 如实报错，不静默退回。
  Result<PiecewiseParseResult> overTower = tower_parser_detail::parseAbsoluteValueOverTower(text);
  if (overTower.isOk()) {
    return overTower.unwrap();
  }
  if (overTower.unwrapErr() != MathsError::InvalidExpression) {
    return std::unexpected(overTower.unwrapErr());
  }

  // 原文里有没有根号 —— 用来把「根号里含多个变量」翻译成「绝对值这边只支持一元」：
  // `|a+b|` 走的是内部形式 `√((a+b)²)`，用户压根没打根号，报「根号里含多个变量」
  // 会让人莫名其妙。查的是**原文**，不是改写后那份。
  const bool typedRadical = text.find("\\sqrt") != std::string_view::npos;
  const auto reported = [typedRadical](MathsError error) {
    return (!typedRadical && error == MathsError::MultiVariableRadical) ? MathsError::NotUnivariate : error;
  };

  const std::string normalized = expression_detail::normalizeLatex(radical_branch_detail::rewriteAbsoluteValues(text));

  // 先按常规路径试一次：没有完全平方的根号时直接成功，旧行为原封不动地保留
  Result<RadicalExtension> direct = parseRadicalExpression(normalized);
  if (direct.isOk()) {
    Result<RealFunction> single = RealFunction::make(direct.unwrap());
    if (single.isErr()) {
      return std::unexpected(reported(single.unwrapErr()));
    }
    return Result<PiecewiseParseResult>(
        PiecewiseParseResult{PiecewiseFunction::make({single.unwrap()}).unwrap(), false});
  }
  if (direct.unwrapErr() != MathsError::RadicandIsSquare) {
    return std::unexpected(reported(direct.unwrapErr()));
  }

  // 扫出所有 `\sqrt{...}`，把「被开方数是完全平方」的那些换成占位变量
  std::string rewritten;
  std::vector<radical_branch_detail::SquareRadical> squares;
  std::size_t cursor = 0;
  while (cursor < normalized.size()) {
    const std::size_t at = normalized.find("\\sqrt", cursor);
    if (at == std::string::npos) {
      rewritten.append(normalized, cursor, std::string::npos);
      break;
    }
    rewritten.append(normalized, cursor, at - cursor);

    std::size_t position = at + 5; // 跳过 "\sqrt"
    while (position < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[position])) != 0) {
      ++position;
    }
    std::string radicandText;
    if (position >= normalized.size() || normalized[position] == '[' ||
        !expression_detail::takeBracedGroup(normalized, position, radicandText)) {
      // 不是简单的 `\sqrt{...}`（比如高次根）：原样留着，让后面的解析器去报错
      rewritten.append(normalized, at, position - at);
      cursor = position;
      continue;
    }

    std::optional<RationalFunction> root;
    if (const Result<RationalFunction> radicand = expression_detail::ParserOf<Fraction>(radicandText).parse();
        radicand.isOk()) {
      root = radical_branch_detail::squareRootOf(radicand.unwrap());
    }
    if (!root.has_value()) {
      rewritten.append(normalized, at, position - at); // 不是完全平方：原样保留
      cursor = position;
      continue;
    }
    if (squares.size() >= radical_branch_detail::kMaxSquareRadicals) {
      return std::unexpected(MathsError::RadicandIsSquare); // 分支出数会爆，明确拒绝而不是硬撑
    }

    std::string name = "abs";
    name += static_cast<char>('a' + squares.size());
    while (normalized.find("{" + name + "}") != std::string::npos) {
      name += "z"; // 原文里已经占了这个名字，换一个
    }
    rewritten += "{" + name + "}";
    squares.push_back({name, *root});
    cursor = position;
  }

  Result<RadicalExtension> parsed = parseRadicalExpression(rewritten);
  if (parsed.isErr()) {
    return std::unexpected(reported(parsed.unwrapErr()));
  }

  // 每个「完全平方的根号」两种取法：+g 要求 g ≥ 0，−g 要求 g < 0
  const std::size_t branchCount = std::size_t(1) << squares.size();
  std::vector<RealFunction> branches;
  for (std::size_t mask = 0; mask < branchCount; ++mask) {
    Scope scope;
    Result<RealSet> domain = Result<RealSet>(RealSet::realLine());
    for (std::size_t index = 0; index < squares.size(); ++index) {
      const bool negative = (mask & (std::size_t(1) << index)) != 0;
      Result<void> assigned =
          scope.assign(Variable(squares[index].placeholder), negative ? -squares[index].root : squares[index].root);
      if (assigned.isErr()) {
        return std::unexpected(assigned.unwrapErr());
      }
      Result<RealSet> condition =
          solveInequality(squares[index].root, negative ? Relation::Less : Relation::GreaterEqual);
      if (condition.isErr()) {
        return std::unexpected(condition.unwrapErr());
      }
      Result<RealSet> narrowed = domain.unwrap().intersect(condition.unwrap());
      if (narrowed.isErr()) {
        return std::unexpected(narrowed.unwrapErr());
      }
      domain = narrowed.unwrap();
    }

    Result<RadicalExtension> substituted = parsed.unwrap().substitute(scope);
    if (substituted.isErr()) {
      return std::unexpected(substituted.unwrapErr());
    }
    Result<RealFunction> branch = RealFunction::make(substituted.unwrap(), domain.unwrap());
    if (branch.isErr()) {
      return std::unexpected(branch.unwrapErr());
    }
    branches.push_back(branch.unwrap()); // 符号互相冲突的那些支定义域为空，由 make 丢掉
  }
  Result<PiecewiseFunction> built = PiecewiseFunction::make(std::move(branches));
  if (built.isErr()) {
    return std::unexpected(built.unwrapErr());
  }
  // 改写过（有 `|…|` 或 `√(g²)` 被换成占位变量）就是「按绝对值写的」
  return PiecewiseParseResult{built.unwrap(), !squares.empty()};
}

// 只要分段本身时的便捷入口
inline Result<PiecewiseFunction> parsePiecewiseExpression(std::string_view text) {
  Result<PiecewiseParseResult> detailed = parsePiecewiseExpressionDetailed(text);
  if (detailed.isErr()) {
    return std::unexpected(detailed.unwrapErr());
  }
  return detailed.unwrap().value;
}

// ==================== 套嵌根号 → 塔 ====================

namespace tower_parser_detail {

// 整条式子恰好是 `|g|`（g 里带根号）时，按符号分支给出分段函数。
//
// 为什么不能走「改写成 `√(g²)`」那条路：g 自带根号时内部形式会**套嵌**，代数函数域装不下；
// 就算硬塞进塔，`y² = (√x)²` 造出的那一层是**退化**的 —— 两个生成元表示同一个根，
// y 到底是 +y₁ 还是 −y₁ 说不清。
//
// 直接对 g 分支就没这个问题：`|g| = { g 当 g≥0 ; −g 当 g<0 }`。
// 形状不匹配时返回 InvalidExpression，让调用方继续走原来的改写路线；
// 形状对但判不了符号时如实报错，不静默退回。
inline Result<PiecewiseParseResult> parseAbsoluteValueOverTower(std::string_view text) {
  const std::string normalized = expression_detail::normalizeLatex(text);
  if (normalized.size() < 2 || normalized.front() != '|' || normalized.back() != '|') {
    return Result<PiecewiseParseResult>::err(MathsError::InvalidExpression);
  }
  const std::string inner = normalized.substr(1, normalized.size() - 2);
  if (inner.find('|') != std::string::npos || inner.find("\\sqrt") == std::string::npos) {
    return Result<PiecewiseParseResult>::err(MathsError::InvalidExpression);
  }
  Result<TowerExtension> value = parseTowerExpression(inner);
  if (value.isErr()) {
    return Result<PiecewiseParseResult>::err(value.unwrapErr());
  }
  Result<RealSet> nonNegative = whereNonNegativeOverTower(value.unwrap());
  if (nonNegative.isErr()) {
    return Result<PiecewiseParseResult>::err(nonNegative.unwrapErr());
  }
  Result<RealSet> domain = domainOf(value.unwrap());
  if (domain.isErr()) {
    return Result<PiecewiseParseResult>::err(domain.unwrapErr());
  }
  Result<RealSet> inside = nonNegative.unwrap().intersect(domain.unwrap());
  if (inside.isErr()) {
    return Result<PiecewiseParseResult>::err(inside.unwrapErr());
  }
  // 外面那支 = 定义域 ∩（g≥0 的补集）
  Result<RealSet> rest = inside.unwrap().complement();
  if (rest.isErr()) {
    return Result<PiecewiseParseResult>::err(rest.unwrapErr());
  }
  Result<RealSet> outside = rest.unwrap().intersect(domain.unwrap());
  if (outside.isErr()) {
    return Result<PiecewiseParseResult>::err(outside.unwrapErr());
  }
  Result<RealFunction> positive = RealFunction::make(FunctionRule::towerOf(value.unwrap()), inside.unwrap());
  if (positive.isErr()) {
    return Result<PiecewiseParseResult>::err(positive.unwrapErr());
  }
  Result<RealFunction> negative = RealFunction::make(FunctionRule::towerOf(value.unwrap().negate()), outside.unwrap());
  if (negative.isErr()) {
    return Result<PiecewiseParseResult>::err(negative.unwrapErr());
  }
  Result<PiecewiseFunction> built = PiecewiseFunction::make({positive.unwrap(), negative.unwrap()});
  if (built.isErr()) {
    return Result<PiecewiseParseResult>::err(built.unwrapErr());
  }
  return Result<PiecewiseParseResult>(PiecewiseParseResult{built.unwrap(), true});
}

// 最里层的那个 `\sqrt{...}`（内容里不再有根号）。
// 找到就返回它在 text 里的 [spanBegin, spanEnd) 跨度（spanEnd 落在 `}` 之后）与内容。
inline bool innermostRadical(const std::string &text, std::size_t &spanBegin, std::size_t &spanEnd,
                             std::string &content) {
  std::size_t search = 0;
  while (true) {
    const std::size_t at = text.find("\\sqrt", search);
    if (at == std::string::npos) {
      return false;
    }
    std::size_t position = at + 5;
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0) {
      ++position;
    }
    if (position < text.size() && text[position] == '[') {
      return false; // 高次根不做
    }
    std::string inner;
    if (!expression_detail::takeBracedGroup(text, position, inner)) {
      return false;
    }
    if (inner.find("\\sqrt") == std::string::npos) {
      spanBegin = at;
      spanEnd = position;
      content = std::move(inner);
      return true;
    }
    // 这个不是最里层，继续往**它内部**找 —— 从 at+1 起扫，而不是从 position 起
    // （内层根号在 position 之前，从 position 起会直接跳过它，整轮一个都找不到）
    search = at + 1;
  }
}

// 一个原文里没用过的占位变量名。
//
// ⚠️ 必须**纯字母**：`Name::check` 只接受「全字母」或「全数字」，混着写会抛
// `InvalidName`。写成 `{zabsa}` 这种带花括号的长名，是为了不跟邻近字母粘成隐含乘法。
inline Variable freshPlaceholder(const std::string &text, std::size_t index) {
  std::string name = "zabs";
  name += static_cast<char>('a' + index);
  while (text.find("{" + name + "}") != std::string::npos) {
    name += "z";
  }
  return Variable(name);
}

} // namespace tower_parser_detail

// 把含**套嵌根号**的式子解析成塔：`\sqrt{1+\sqrt{x}}` → y₁² = x、y₂² = 1 + y₁。
//
// 由内往外逐个根号处理：
//   1. 找最里层的 `\sqrt{...}`，内容记下来，整个 span 换成占位变量
//   2. 内容里已经没有根号 → 按有理函数解析，用 `evaluateOverPlaceholders`
//      把已用过的占位变量换成对应生成元
//   3. `adjoining` 往上接一层
//   4. 最后整条式子同样代一遍，得到元素本身
//
// 只处理**一元**（与库里其他函数类型一致）。`\sqrt{2}` 这类纯数值根号走实代数数那条路，
// 不归这里。
inline Result<TowerExtension> parseTowerExpression(std::string_view text) {
  using tower_parser_detail::freshPlaceholder;
  using tower_parser_detail::innermostRadical;

  std::string remaining = expression_detail::normalizeLatex(text);
  std::vector<std::string> radicands; // 由内往外
  std::vector<Variable> placeholders;

  while (true) {
    std::size_t spanBegin = 0;
    std::size_t spanEnd = 0;
    std::string content;
    if (!innermostRadical(remaining, spanBegin, spanEnd, content)) {
      break;
    }
    const Variable placeholder = freshPlaceholder(remaining, placeholders.size());
    placeholders.push_back(placeholder);
    radicands.push_back(std::move(content));
    remaining = remaining.substr(0, spanBegin) + "{" + placeholder.str() + "}" + remaining.substr(spanEnd);
  }
  if (placeholders.empty()) {
    return Result<TowerExtension>::err(MathsError::InvalidExpression); // 没有根号，走别的入口
  }

  // 由内往外往上接层
  Result<TowerExtension> tower = TowerExtension::rational(RationalFunction(Fraction(0, 1)));
  for (std::size_t index = 0; index < placeholders.size(); ++index) {
    Result<RationalFunction> parsed = expression_detail::ParserOf<Fraction>(radicands[index]).parse();
    if (parsed.isErr()) {
      return std::unexpected(parsed.unwrapErr());
    }
    const std::vector<Variable> used(placeholders.begin(), placeholders.begin() + static_cast<std::ptrdiff_t>(index));
    Result<TowerExtension> radicand = evaluateOverPlaceholders(parsed.unwrap(), used, tower.unwrap());
    if (radicand.isErr()) {
      return radicand;
    }
    Result<TowerExtension> appended = tower.unwrap().adjoiningElement(radicand.unwrap());
    if (appended.isErr()) {
      return std::unexpected(appended.unwrapErr());
    }
    tower = appended;
  }

  // 整条式子：代掉全部占位变量
  Result<RationalFunction> whole = expression_detail::ParserOf<Fraction>(remaining).parse();
  if (whole.isErr()) {
    return std::unexpected(whole.unwrapErr());
  }
  return evaluateOverPlaceholders(whole.unwrap(), placeholders, tower.unwrap());
}

// ==================== 多元式子 → 多元塔 ====================
//
// 与一元的 `parseTowerExpression` 同一套思路：由内往外逐个根号处理。
// 换掉的只是**占位变量的替换方式** —— 一元那边系数是 `RationalFunction`，
// 多元这边是 `MultiRationalFunction`，两者都在 x 的有理函数域里，所以
// 「逐项展开成 系数 × 生成元的幂」那一招照样管用。

namespace multi_parser_detail {

// 原文里没用过的单字母占位名。
//
// ⚠️ 必须**纯字母**：`Name::check` 只接受「全字母」或「全数字」，混着写会抛
// `InvalidName`（一元那边踩过：名字带数字 → 整个进程 abort、无任何输出）。
inline Variable freshPlaceholder(const std::string &text, std::size_t index) {
  std::string name = "zabs";
  name += static_cast<char>('a' + index);
  while (text.find("{" + name + "}") != std::string::npos) {
    name += "z";
  }
  return Variable(name);
}

// 最里层的那个 `\sqrt{...}`（内容里不再有根号），返回 [spanBegin, spanEnd) 与内容
inline bool innermostRadical(const std::string &text, std::size_t &spanBegin, std::size_t &spanEnd,
                             std::string &content) {
  std::size_t search = 0;
  while (true) {
    const std::size_t at = text.find("\\sqrt", search);
    if (at == std::string::npos) {
      return false;
    }
    std::size_t position = at + 5;
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0) {
      ++position;
    }
    if (position < text.size() && text[position] == '[') {
      return false; // 高次根不做
    }
    std::string inner;
    if (!expression_detail::takeBracedGroup(text, position, inner)) {
      return false;
    }
    if (inner.find("\\sqrt") == std::string::npos) {
      spanBegin = at;
      spanEnd = position;
      content = std::move(inner);
      return true;
    }
    // 这个不是最里层，继续往**它内部**找 —— 从 at+1 起扫，不是从 position 起
    // （内层根号在 position 之前，从 position 起会直接跳过它）
    search = at + 1;
  }
}

// 把「含占位变量的多元有理函数」变成塔里的元素：逐项展开，最后分子 ÷ 分母。
//
// 不能「先换 t₁ 再换 t₂」—— 换出来的元素不是有理函数，没地方放回去。
// 逐项展开则全程都是「系数 × 生成元的幂」，可以累加。
Result<MultiTowerExtension> substitutePlaceholders(const MultiRationalFunction &value,
                                                   const std::vector<Variable> &placeholders,
                                                   const MultiTowerExtension &tower) {
  const auto expand = [&placeholders, &tower](const Polynomial &polynomial) -> Result<MultiTowerExtension> {
    std::optional<MultiTowerExtension> total;
    for (const auto &[factors, coefficient] : polynomial.getTerms()) {
      std::vector<unsigned> powers(tower.depth(), 0);
      VarPowers rest;
      for (const auto &[variable, power] : factors) {
        bool isPlaceholder = false;
        for (std::size_t index = 0; index < placeholders.size(); ++index) {
          if (placeholders[index] == variable) {
            if (index >= tower.depth()) {
              return Result<MultiTowerExtension>::err(MathsError::NestedRadical);
            }
            powers[index] += power;
            isPlaceholder = true;
            break;
          }
        }
        if (!isPlaceholder) {
          rest.push_back({variable, power});
        }
      }
      Result<MultiTowerExtension> term = tower.lifting(MultiRationalFunction(Monomial(coefficient, rest)));
      for (std::size_t index = 0; index < powers.size(); ++index) {
        for (unsigned step = 0; step < powers[index]; ++step) {
          if (term.isErr()) {
            return term;
          }
          Result<MultiTowerExtension> generator = MultiTowerExtension::generatorOf(tower, index);
          if (generator.isErr()) {
            return generator;
          }
          Result<MultiTowerExtension> product = term.unwrap() * generator.unwrap();
          if (product.isErr()) {
            return product;
          }
          term = product;
        }
      }
      if (term.isErr()) {
        return term;
      }
      if (!total.has_value()) {
        total = term.unwrap();
        continue;
      }
      Result<MultiTowerExtension> sum = total.value() + term.unwrap();
      if (sum.isErr()) {
        return sum;
      }
      total = sum.unwrap();
    }
    if (!total.has_value()) {
      Result<MultiTowerExtension> zero = tower.lifting(MultiRationalFunction(Fraction(0, 1)));
      return zero;
    }
    return Result<MultiTowerExtension>(total.value());
  };

  Result<MultiTowerExtension> numerator = expand(value.numerator());
  if (numerator.isErr()) {
    return numerator;
  }
  Result<MultiTowerExtension> denominator = expand(value.denominator());
  if (denominator.isErr()) {
    return denominator;
  }
  if (denominator.unwrap().isZero()) {
    return Result<MultiTowerExtension>::err(MathsError::ZeroDenominator);
  }
  return numerator.unwrap().dividedBy(denominator.unwrap());
}

} // namespace multi_parser_detail

// 把含**套嵌根号**的多元式子解析成多元塔：`\sqrt{1+\sqrt{x^2+y^2}}` →
// y₁² = x²+y²、y₂² = 1 + y₁。
inline Result<MultiTowerExtension> parseMultiTowerExpression(std::string_view text) {
  using multi_parser_detail::freshPlaceholder;
  using multi_parser_detail::innermostRadical;
  using multi_parser_detail::substitutePlaceholders;

  std::string remaining = expression_detail::normalizeLatex(text);
  std::vector<std::string> radicands; // 由内往外
  std::vector<Variable> placeholders;

  while (true) {
    std::size_t spanBegin = 0;
    std::size_t spanEnd = 0;
    std::string content;
    if (!innermostRadical(remaining, spanBegin, spanEnd, content)) {
      break;
    }
    const Variable placeholder = freshPlaceholder(remaining, placeholders.size());
    placeholders.push_back(placeholder);
    radicands.push_back(std::move(content));
    remaining = remaining.substr(0, spanBegin) + "{" + placeholder.str() + "}" + remaining.substr(spanEnd);
  }
  if (placeholders.empty()) {
    return Result<MultiTowerExtension>::err(MathsError::InvalidExpression); // 没有根号，走别的入口
  }

  // 由内往外往上接层
  Result<MultiTowerExtension> tower = MultiTowerExtension::rational(MultiRationalFunction(Fraction(0, 1)));
  for (std::size_t index = 0; index < placeholders.size(); ++index) {
    Result<RationalFunction> parsed = expression_detail::ParserOf<Fraction>(radicands[index]).parse();
    if (parsed.isErr()) {
      return std::unexpected(parsed.unwrapErr());
    }
    Result<MultiRationalFunction> asMulti =
        MultiRationalFunction::make(parsed.unwrap().getNumerator(), parsed.unwrap().getDenominator());
    if (asMulti.isErr()) {
      return std::unexpected(asMulti.unwrapErr());
    }
    const std::vector<Variable> used(placeholders.begin(), placeholders.begin() + static_cast<std::ptrdiff_t>(index));
    Result<MultiTowerExtension> radicand = substitutePlaceholders(asMulti.unwrap(), used, tower.unwrap());
    if (radicand.isErr()) {
      return radicand;
    }
    Result<MultiTowerExtension> appended = tower.unwrap().adjoiningElement(radicand.unwrap());
    if (appended.isErr()) {
      return std::unexpected(appended.unwrapErr());
    }
    tower = appended;
  }

  // 整条式子：代掉全部占位变量
  Result<RationalFunction> whole = expression_detail::ParserOf<Fraction>(remaining).parse();
  if (whole.isErr()) {
    return std::unexpected(whole.unwrapErr());
  }
  Result<MultiRationalFunction> asMulti =
      MultiRationalFunction::make(whole.unwrap().getNumerator(), whole.unwrap().getDenominator());
  if (asMulti.isErr()) {
    return std::unexpected(asMulti.unwrapErr());
  }
  return substitutePlaceholders(asMulti.unwrap(), placeholders, tower.unwrap());
}

// ==================== 多元分段函数 ====================
//
// `|f|` 在多元里是能表示的：`{ f : {f ≥ 0} , −f : {f < 0} }`。
//
// ⚠️ 别把它当成「需要 CAD」。**表示本身不需要判定符号** —— 那两支的定义域
// 就是符号本身写着呢。只有「化简」（合并区域、判哪支是空集）才需要符号判定。
// 而 `g = N/D` 的符号条件精确且廉价：`g ≥ 0` ⟺ `N·D ≥ 0 ∧ D ≠ 0`（同号）。
//
// （一元那边 `squareRootOf` 里写着「多变量：分段函数是一元的，装不下」——
//   那句判断是错的：多元分段类型就在同一个库里，连分支两两不交的校验都写好了，
//   只是没人写这个解析器。)
inline Result<MultiPiecewiseFunction> parseMultiPiecewiseExpression(std::string_view text) {
  if (text.find('|') == std::string_view::npos) {
    return std::unexpected(MathsError::InvalidExpression); // 没绝对值就不是这一档
  }
  // `|g|` → `\sqrt{g^2}`。之后只认「内容字面以 ^2 结尾」的根号 —— 那是
  // rewriteAbsoluteValues 自己造出来的形态，认它就够了，不去做多项式开方（那是因式分解）。
  const std::string normalized = expression_detail::normalizeLatex(radical_branch_detail::rewriteAbsoluteValues(text));
  if (normalized.find("\\sqrt") == std::string::npos) {
    return std::unexpected(MathsError::InvalidExpression);
  }

  struct SquareRoot {
    std::string name; // 占位名（不含花括号）
    std::string body; // 开方前的式子原文
    MultiRationalFunction body_value;
  };
  std::vector<SquareRoot> squares;
  std::string rewritten;
  std::size_t cursor = 0;
  while (true) {
    const std::size_t at = normalized.find("\\sqrt", cursor);
    if (at == std::string::npos) {
      rewritten += normalized.substr(cursor);
      break;
    }
    // ⚠️ `\sqrt` 是 **5** 个字符。写成 6 会越过 `{` 落在 `(` 上，
    // takeBracedGroup 立刻失败 —— 表现为「一个平方根号都找不到」。
    constexpr std::size_t kRadicalKeyword = 5;
    std::size_t position = at + kRadicalKeyword;
    while (position < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[position])) != 0) {
      ++position;
    }
    std::string radicand;
    if (position >= normalized.size() || normalized[position] == '[' ||
        !expression_detail::takeBracedGroup(normalized, position, radicand)) {
      rewritten += normalized.substr(cursor, position - cursor);
      cursor = position;
      continue;
    }
    // 有理解析出来的 numerator/denominator 是 `Result<Polynomial>`（多项式乘法可能失败），
    // 这里要的是**多元**有理函数，所以再走一次 make —— 与 parseMultiTowerExpression 同一手法。
    Result<MultiRationalFunction> body = Result<MultiRationalFunction>::err(MathsError::InvalidExpression);
    if (radicand.size() >= 2 && radicand.compare(radicand.size() - 2, 2, "^2") == 0) {
      Result<RationalFunction> parsed = parseExpression(radicand.substr(0, radicand.size() - 2));
      if (parsed.isOk()) {
        body = MultiRationalFunction::make(parsed.unwrap().getNumerator(), parsed.unwrap().getDenominator());
      }
    }
    if (body.isErr()) {
      rewritten += normalized.substr(at, position - at); // 不是完全平方：原样留给别的档
      cursor = position;
      continue;
    }
    if (squares.size() >= radical_branch_detail::kMaxSquareRadicals) {
      return std::unexpected(MathsError::RadicandIsSquare); // 分支数会爆，明确拒绝
    }
    std::string name = "zabs";
    name += static_cast<char>('a' + squares.size());
    while (normalized.find("{" + name + "}") != std::string::npos) {
      name += "z"; // 原文里占了这个名字
    }
    rewritten += "{" + name + "}";
    squares.push_back({name, radicand.substr(0, radicand.size() - 2), body.unwrap()});
    cursor = position;
  }

  if (squares.empty()) {
    return std::unexpected(MathsError::InvalidExpression);
  }

  // ⚠️ **完全平方的短路**：`x^2+y^2-2xy` 这种输入，分子是完全平方、分母是正的常数，
  // 于是 `|f| = f` 恒成立 —— 不必拆成两支。判据是 `squareRootIfSquare`（见 groebner 模块）。
  //
  // 为什么这**不是**符号判定：判「f ≥ 0 恒成立」是 CAD 那一类；而判「分子是完全平方」
  // 只是代数事实，一个 gcd 就够（Yun 平方自由分解的第一步）。**表示与判定是两件事。**
  //
  // ⚠️ `squareRootIfSquare` 目前**有漏判**（`x^4`、`x^2y^2` 这类含多重因子的情况还算不对），
  // 漏判的后果是「该走短路时按分支处理」—— 结果仍然正确，只是没化简。所以别把
  // 「它说不是平方」当成「这不是平方」。
  if (squares.size() == std::size_t(1)) {
    const Result<MultiRationalFunction> single =
        MultiRationalFunction::make(parseExpression(squares.front().body).unwrap().getNumerator(),
                                    parseExpression(squares.front().body).unwrap().getDenominator());
    if (single.isOk()) {
      const MultiRationalFunction &value = single.unwrap();
      const std::optional<Polynomial> root = squareRootIfSquare(value.numerator());
      if (root.has_value() && value.denominator().variables().empty()) {
        // 分母是常数：只要它为正，f 就恒非负
        const std::map<VarPowers, Fraction> &terms = value.denominator().getTerms();
        if (terms.size() == std::size_t(1) && terms.begin()->first.empty() && terms.begin()->second > Fraction(0, 1)) {
          const Result<Region> whole = Region::wholeSpace();
          if (whole.isOk()) {
            return MultiPiecewiseFunction::make({MultiPiecewiseFunction::Branch{MultiRule(value), whole.unwrap()}});
          }
        }
      }
    }
  }

  // 枚举 ±：第 index 位取负的那支要求 g_index < 0，其余要求 ≥ 0
  std::vector<MultiPiecewiseFunction::Branch> branches;
  for (std::size_t mask = 0; mask < (std::size_t(1) << squares.size()); ++mask) {
    std::string substituted = rewritten;
    ConstraintSystem system;
    for (std::size_t index = 0; index < squares.size(); ++index) {
      const bool negative = (mask & (std::size_t(1) << index)) != 0;
      const std::string token = "{" + squares[index].name + "}";
      const std::string replacement = negative ? "-(" + squares[index].body + ")" : "(" + squares[index].body + ")";
      for (std::size_t at = substituted.find(token); at != std::string::npos;
           at = substituted.find(token, at + replacement.size())) {
        substituted.replace(at, token.size(), replacement);
      }
      // g = N/D：符号条件用 N·D（同号），分母不能为 0
      const MultiRationalFunction &value = squares[index].body_value;
      Result<Polynomial> product = value.numerator() * value.denominator();
      if (product.isErr()) {
        return std::unexpected(product.unwrapErr());
      }
      system = system.andWith(
          ConstraintSystem({AtomConstraint(product.unwrap(), negative ? Relation::Less : Relation::GreaterEqual)}));
      system = system.andWith(ConstraintSystem({AtomConstraint(value.denominator(), Relation::NotEqual)}));
    }
    Result<RationalFunction> parsed = parseExpression(substituted);
    if (parsed.isErr()) {
      return std::unexpected(parsed.unwrapErr());
    }
    Result<MultiRationalFunction> rule =
        MultiRationalFunction::make(parsed.unwrap().getNumerator(), parsed.unwrap().getDenominator());
    if (rule.isErr()) {
      return std::unexpected(rule.unwrapErr());
    }
    Result<Region> domain = Region::fromSystem(system);
    if (domain.isErr()) {
      return std::unexpected(domain.unwrapErr());
    }
    branches.push_back(MultiPiecewiseFunction::Branch{MultiRule(rule.unwrap()), domain.unwrap()});
  }
  return MultiPiecewiseFunction::make(std::move(branches));
}

struct Assignment {
  Variable variable;
  RationalFunction value; // 右边可以是含其它变量的表达式，如 s = v*t
};

// 解析代入条件 "变量 = 表达式"。
// 返回 nullopt 表示这条输入没有可记录的信息（常数恒等式）。
// 两侧可交换：C = x 会被解释为 x = C。
// 需要解方程的形式（x + 1 = 2）不被支持，返回 InvalidExpression。
// x = x 不是赋值而是「解除该变量的绑定」，见 parseErase。
inline Result<std::optional<Assignment>> parseAssignment(std::string_view text) {
  const std::size_t equals = text.find('=');
  if (equals == std::string_view::npos) {
    return std::unexpected(MathsError::InvalidExpression);
  }
  if (text.find('=', equals + 1) != std::string_view::npos) {
    return std::unexpected(MathsError::InvalidExpression); // 不支持 == 这类写法
  }

  Result<RationalFunction> leftHand = parseExpression(text.substr(0, equals));
  if (leftHand.isErr()) {
    return std::unexpected(leftHand.unwrapErr());
  }
  Result<RationalFunction> rightHand = parseExpression(text.substr(equals + 1));
  if (rightHand.isErr()) {
    return std::unexpected(rightHand.unwrapErr());
  }

  const RationalFunction &left = leftHand.unwrap();
  const RationalFunction &right = rightHand.unwrap();

  const std::optional<Variable> leftVariable = expression_detail::asSingleVariable(left);
  const std::optional<Variable> rightVariable = expression_detail::asSingleVariable(right);
  const std::optional<Fraction> leftConstant = expression_detail::asConstant(left);
  const std::optional<Fraction> rightConstant = expression_detail::asConstant(right);

  // x = x：同一个变量。解除绑定由 parseErase 负责，这里按「没有可记录的信息」处理
  if (leftVariable && rightVariable && *leftVariable == *rightVariable) {
    return std::optional<Assignment>{};
  }

  // C = C：同一个常数，恒等式
  if (leftConstant && rightConstant && *leftConstant == *rightConstant) {
    return std::optional<Assignment>{};
  }

  // 变量 = 表达式。右边不得含被赋值的变量本身 —— 那是方程而非赋值
  // （x = 2x、x = x + 1 都需要解方程，明确不支持）。
  if (leftVariable) {
    if (right.containsVariable(*leftVariable)) {
      return std::unexpected(MathsError::NotAnAssignment);
    }
    return std::optional<Assignment>{Assignment{*leftVariable, right}};
  }

  // 表达式 = 变量：只要左边不含该变量，就能把变量解到右边（vt = x 即 x = vt，
  // x + 1 = y 即 y = x + 1 —— y 单独在一侧，直接读出，不算解方程）。
  // 左边含它的话（如 x + 1 = x）才是真正的方程，明确不支持。
  if (rightVariable && !left.containsVariable(*rightVariable)) {
    return std::optional<Assignment>{Assignment{*rightVariable, left}};
  }

  return std::unexpected(MathsError::InvalidExpression);
}

// 解析「解除绑定」的输入：x = x 表示删掉变量 x 此前记录的约束（y = y、x_1 = x_1 同理）。
// 返回 nullopt 表示这条输入不是删除指令，调用方应继续按赋值解析。
//
// 与常数恒等式（5 = 5）区分：后者两边是同一个常数，不涉及变量，不是删除指令。
// 解析失败的输入（如 x + 1 = 2）也返回 nullopt —— 报错交给 parseAssignment，
// 那里能给出准确的错误码，这里不该抢先判定。
inline std::optional<Variable> parseErase(std::string_view text) {
  const std::size_t equals = text.find('=');
  if (equals == std::string_view::npos || text.find('=', equals + 1) != std::string_view::npos) {
    return std::nullopt;
  }

  Result<RationalFunction> leftHand = parseExpression(text.substr(0, equals));
  Result<RationalFunction> rightHand = parseExpression(text.substr(equals + 1));
  if (leftHand.isErr() || rightHand.isErr()) {
    return std::nullopt;
  }

  const std::optional<Variable> leftVariable = expression_detail::asSingleVariable(leftHand.unwrap());
  const std::optional<Variable> rightVariable = expression_detail::asSingleVariable(rightHand.unwrap());
  if (leftVariable && rightVariable && *leftVariable == *rightVariable) {
    return *leftVariable;
  }
  return std::nullopt;
}

// 约束是否与式子相关：左边变量出现在式子里，或右边含式子里出现过的变量。
// 两者都不成立时，这条约束对式子毫无影响，记录它没有意义
// （例如式子 2x 配上 s = v*t：s、v、t 都不影响 2x）。
// 约束是否与「式子 + 已有绑定」相关。
// 只看原始式子是不够的：式子 2x 在 x = s 之后，s = v*t 就会影响结果
// —— 因为 x 的有效值已经变成了 s。
inline bool isRelevantTo(const RationalFunction &expression, const Scope &scope, const Assignment &assignment) {
  // 1. 被赋值的变量直接出现在式子里
  if (expression.containsVariable(assignment.variable)) {
    return true;
  }
  // 2. 被赋值的变量出现在某条已有绑定的值里 —— 那条绑定的有效值会变
  for (const auto &entry : scope.bindings()) {
    if (entry.second.containsVariable(assignment.variable)) {
      return true;
    }
  }
  // 3. 右边含式子里出现的变量
  for (const Variable &variable : expression.variables()) {
    if (assignment.value.containsVariable(variable)) {
      return true;
    }
  }
  // 4. 右边含某条已有绑定的变量名 —— 代入链会继续展开
  for (const auto &entry : scope.bindings()) {
    if (assignment.value.containsVariable(entry.first)) {
      return true;
    }
  }
  return false;
}

} // namespace maths
