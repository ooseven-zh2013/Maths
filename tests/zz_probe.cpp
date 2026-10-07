#include <set>
#include <optional>
#include <string>
#include <iostream>
import maths;
using namespace maths;
static void t(const char *text) {
  const Polynomial n = parseExpression(text).unwrap().getNumerator();
  const std::optional<Polynomial> r = squareRootIfSquare(n);
  std::cout << "[" << text << "] N=" << n.latex() << " has=" << r.has_value();
  if (r.has_value()) { std::cout << " 根=" << r.value().latex(); }
  std::cout << "\n";
}
int main() {
  std::cout << std::unitbuf;
  t("x^4y^2");
  t("(x^4y^2)");
  t("x^2y^4");
  t("x^4y^2*(x+y)^2");
  return 0;
}
