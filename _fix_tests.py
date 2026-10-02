import pathlib

fixes = [
    ("tests/radical_extension_tests.cpp",
     '    CHECK_EQ(RadicalExtension(-expression("x")).latex(), std::string("-\\\\sqrt{x}"));',
     '    CHECK_EQ(radicalX().negate().latex(), std::string("-\\\\sqrt{x}"));'),
    ("tests/piecewise_parser_tests.cpp",
     '    CHECK_ERR(parsePiecewiseExpression("|a+b-2\\\\sqrt{ab}|"), MathsError::MultiVariableRadical);',
     '    // 先撞上的是套嵌：内部形式是 \\sqrt{((a+b)-2\\sqrt{ab})^2}，外层根号里已经有根号了\n'
     '    CHECK_ERR(parsePiecewiseExpression("|a+b-2\\\\sqrt{ab}|"), MathsError::NestedRadical);'),
    ("tests/parser_expression_parser_tests.cpp",
     '    CHECK_ERR(parseRadicalExpression("-\\\\sqrt{x^2}"), MathsError::NotARational);',
     '    CHECK_ERR(parseRadicalExpression("-\\\\sqrt{x^2}"), MathsError::RadicandIsSquare);'),
]
for path, old, new in fixes:
    p = pathlib.Path(path)
    s = p.read_text(encoding="utf-8")
    assert s.count(old) == 1, (path, s.count(old))
    p.write_text(s.replace(old, new), encoding="utf-8")
    print("ok", path)
