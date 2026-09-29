export module maths.parser;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebra;
import maths.algebraic_number;

export namespace maths {

// 简易表达式解析器：把文本解析成 RationalFunction。
//
// 支持的语法：
//   expr    := term (('+' | '-') term)*
//   term    := power (('*' | '/') power)*
//   power   := unary ('^' 非负整数)?
//   unary   := ('-' | '+')? primary
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
    Result<RationalFunctionOf<Coefficient>> left = parsePower();
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

      Result<RationalFunctionOf<Coefficient>> right = parsePower();
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

  Result<RationalFunctionOf<Coefficient>> parsePower() {
    Result<RationalFunctionOf<Coefficient>> base = parseUnary();
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
    return parsePrimary();
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
    const Result<RationalFunctionOf<Coefficient>> radicand = ParserOf<Coefficient>(radicandText).parse();
    if (radicand.isErr()) {
      return radicand;
    }
    const Result<MonomialOf<Coefficient>> numerator = radicand.unwrap().getNumerator().toMonomial();
    const Result<MonomialOf<Coefficient>> denominator = radicand.unwrap().getDenominator().toMonomial();
    if (numerator.isErr() || !numerator.unwrap().isConstant() || denominator.isErr() ||
        !denominator.unwrap().isConstant()) {
      return std::unexpected(MathsError::InvalidExpression); // \sqrt{x} 这类：本库表示不了
    }

    const Result<Coefficient> value = numerator.unwrap().getCoefficient() / denominator.unwrap().getCoefficient();
    if (value.isErr()) {
      return std::unexpected(value.unwrapErr());
    }
    const Result<Coefficient> root = coefficientNthRoot(value.unwrap(), degree);
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
  const Result<Monomial> denominator = value.getDenominator().toMonomial();
  if (denominator.isErr() || !denominator.unwrap().isConstant() || denominator.unwrap().getCoefficient() != 1LL) {
    return std::nullopt;
  }

  const Result<Monomial> numerator = value.getNumerator().toMonomial();
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
  const Result<Fraction> evaluated = value.evaluate(Scope());
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
    Result<RadicalExtension> left = parsePower();
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
      Result<RadicalExtension> right = parsePower();
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

  Result<RadicalExtension> parsePower() {
    Result<RadicalExtension> base = parseUnary();
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
      const Result<RadicalExtension> next = result * base.unwrap();
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
    return parseAtom();
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
        return std::unexpected(MathsError::InvalidExpression); // 嵌套根号暂不支持
      }
      const Result<RationalFunction> radicand = ParserOf<Fraction>(radicandText).parse();
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
    const Result<RationalFunction> parsed = ParserOf<Fraction>(chunk).parse();
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

  const Result<RationalFunction> leftHand = parseExpression(text.substr(0, equals));
  const Result<RationalFunction> rightHand = parseExpression(text.substr(equals + 1));
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
