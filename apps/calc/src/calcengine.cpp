#include "calcengine.h"

#include <QtMath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace {

const Num Pi = std::acos(Num(-1));

const QString DivideByZero = QStringLiteral("Cannot divide by zero.");
const QString InvalidInput = QStringLiteral("Invalid input for function.");
const QString Overflow = QStringLiteral("Overflow.");

constexpr int MaxDecimalDigits = 16; // what a Num holds exactly

bool isBinary(const QString &k)
{
    static const QStringList ops{QStringLiteral("+"), QStringLiteral("-"), QStringLiteral("*"), QStringLiteral("/"),
                                 QStringLiteral("Mod"), QStringLiteral("And"), QStringLiteral("Or"), QStringLiteral("Xor"),
                                 QStringLiteral("Lsh"), QStringLiteral("x^y")};
    return ops.contains(k);
}

bool isUnary(const QString &k)
{
    static const QStringList fns{QStringLiteral("sqrt"), QStringLiteral("%"), QStringLiteral("1/x"), QStringLiteral("sin"),
                                 QStringLiteral("cos"), QStringLiteral("tan"), QStringLiteral("x^2"), QStringLiteral("x^3"),
                                 QStringLiteral("ln"), QStringLiteral("log"), QStringLiteral("n!"), QStringLiteral("Int"),
                                 QStringLiteral("Not")};
    return fns.contains(k);
}

qint64 toInt(Num v)
{
    // what an integer mode makes of a number: truncated, wrapped to 64 bits
    if (!std::isfinite(v))
        return 0;
    const Num t = std::trunc(v);
    if (t >= 9223372036854775807.0L)
        return std::numeric_limits<qint64>::max();
    if (t <= -9223372036854775808.0L)
        return std::numeric_limits<qint64>::min();
    return qint64(t);
}

} // namespace

CalcEngine::CalcEngine(QObject *parent)
    : QObject(parent)
{
    clearAll();
}

void CalcEngine::clearAll()
{
    m_entry.clear();
    m_value = 0;
    m_haveOperand = true;
    m_values.clear();
    m_ops.clear();
    m_lastOp.clear();
    m_lastOperand = 0;
    m_error.clear();
    m_inv = m_hyp = false;
}

void CalcEngine::fail(const QString &message)
{
    m_error = message;
    m_entry.clear();
    m_values.clear();
    m_ops.clear();
    m_lastOp.clear();
}

int CalcEngine::openBrackets() const
{
    return int(m_ops.count(QStringLiteral("(")));
}

// ---------------------------------------------------------------- settings

void CalcEngine::setScientific(bool on)
{
    if (on == m_scientific)
        return;
    m_scientific = on;
    if (!on) {
        // Standard is decimal, left to right, with no brackets
        setBase(10);
        m_inv = m_hyp = false;
        clearAll();
    }
    emit changed();
}

void CalcEngine::setBase(int base)
{
    if (base != 10 && base != 16 && base != 8 && base != 2)
        return;
    if (base == m_base)
        return;
    const Num v = error() ? 0 : entryValue();
    m_base = base;
    m_entry.clear();
    // integers from here on (what's waiting too)
    m_value = base == 10 ? v : Num(toInt(v));
    if (base != 10)
        for (Num &x : m_values)
            x = Num(toInt(x));
    emit changed();
}

void CalcEngine::setAngle(const QString &angle)
{
    if (angle != m_angle && (angle == QLatin1String("deg") || angle == QLatin1String("rad") || angle == QLatin1String("grad"))) {
        m_angle = angle;
        emit changed();
    }
}

void CalcEngine::setInv(bool on)
{
    if (on != m_inv) {
        m_inv = on;
        emit changed();
    }
}

void CalcEngine::setHyp(bool on)
{
    if (on != m_hyp) {
        m_hyp = on;
        emit changed();
    }
}

void CalcEngine::setGrouping(bool on)
{
    if (on != m_grouping) {
        m_grouping = on;
        emit changed();
    }
}

// ----------------------------------------------------------------- display

QString CalcEngine::format(Num v) const
{
    if (m_base != 10) {
        const qint64 n = toInt(v);
        // negatives as 64-bit two's complement, as Windows shows them
        QString s = QString::number(quint64(n), m_base).toUpper();
        if (m_grouping) {
            const int group = m_base == 10 ? 3 : 4;
            for (int i = int(s.size()) - group; i > 0; i -= group)
                s.insert(i, QLatin1Char(' '));
        }
        return s;
    }
    if (v == 0)
        return QStringLiteral("0");
    auto trimmed = [](QString t) {
        // 2.500 -> 2.5, 3.000 -> 3 (and in a mantissa: 1.50e+20 -> 1.5e+20)
        const int e = int(t.indexOf(QLatin1Char('e')));
        QString mant = e < 0 ? t : t.left(e), exp = e < 0 ? QString() : t.mid(e);
        if (mant.contains(QLatin1Char('.'))) {
            while (mant.endsWith(QLatin1Char('0')))
                mant.chop(1);
            if (mant.endsWith(QLatin1Char('.')))
                mant.chop(1);
        }
        return mant + exp;
    };
    char buf[128];
    QString s;
    const Num a = std::fabs(v);
    if (a < Num(1e16) && a >= Num(1e-9)) {
        // up to 16 significant digits, without e notation
        const int decimals = std::max(0, 15 - int(std::floor(std::log10(a))));
        std::snprintf(buf, sizeof buf, "%.*Lf", decimals, v);
        s = trimmed(QString::fromLatin1(buf));
        if (s == QLatin1String("-0"))
            s = QStringLiteral("0");
    } else {
        std::snprintf(buf, sizeof buf, "%.15Le", v);
        s = trimmed(QString::fromLatin1(buf));
    }
    if (m_grouping && !s.contains(QLatin1Char('e'))) {
        const int neg = s.startsWith(QLatin1Char('-')) ? 1 : 0;
        int end = int(s.indexOf(QLatin1Char('.')));
        if (end < 0)
            end = int(s.size());
        for (int i = end - 3; i > neg; i -= 3)
            s.insert(i, QLatin1Char(','));
    }
    return s;
}

QString CalcEngine::display() const
{
    if (!m_error.isEmpty())
        return m_error;
    if (m_entry.isEmpty())
        return format(m_value);
    if (!m_grouping)
        return m_entry;
    // the digits being typed, grouped
    QString s = m_entry;
    const int neg = s.startsWith(QLatin1Char('-')) ? 1 : 0;
    int end = int(s.indexOf(QLatin1Char('.')));
    if (end < 0)
        end = int(s.size());
    const int group = m_base == 10 ? 3 : 4;
    const QChar sep = m_base == 10 ? QLatin1Char(',') : QLatin1Char(' ');
    for (int i = end - group; i > neg; i -= group)
        s.insert(i, sep);
    return s;
}

QString CalcEngine::copyText() const
{
    if (!m_error.isEmpty())
        return QString();
    if (!m_entry.isEmpty())
        return m_entry;
    const bool g = m_grouping;
    const_cast<CalcEngine *>(this)->m_grouping = false;
    const QString s = format(m_value);
    const_cast<CalcEngine *>(this)->m_grouping = g;
    return s;
}

Num CalcEngine::entryValue() const
{
    if (m_entry.isEmpty())
        return m_value;
    if (m_base == 10)
        return m_entry.toDouble();
    bool ok = false;
    const bool neg = m_entry.startsWith(QLatin1Char('-'));
    const quint64 u = (neg ? m_entry.mid(1) : m_entry).toULongLong(&ok, m_base);
    const qint64 n = qint64(u);
    return Num(neg ? -n : n);
}

Num CalcEngine::clampInt(Num v) const
{
    return m_base == 10 ? v : Num(toInt(v));
}

void CalcEngine::setValue(Num v)
{
    if (!std::isfinite(v)) {
        fail(Overflow);
        return;
    }
    m_entry.clear();
    m_value = clampInt(v);
}

// --------------------------------------------------------------- the keys

int CalcEngine::precedence(const QString &op) const
{
    if (!m_scientific)
        return 1; // left to right, as typed
    if (op == QLatin1String("x^y") || op == QLatin1String("root")) return 7;
    if (op == QLatin1String("*") || op == QLatin1String("/") || op == QLatin1String("Mod")) return 6;
    if (op == QLatin1String("+") || op == QLatin1String("-")) return 5;
    if (op == QLatin1String("Lsh") || op == QLatin1String("Rsh")) return 4;
    if (op == QLatin1String("And")) return 3;
    if (op == QLatin1String("Xor")) return 2;
    if (op == QLatin1String("Or")) return 1;
    return 0;
}

Num CalcEngine::apply(const QString &op, Num a, Num b)
{
    const bool ints = m_base != 10;
    if (op == QLatin1String("+")) return a + b;
    if (op == QLatin1String("-")) return a - b;
    if (op == QLatin1String("*")) return a * b;
    if (op == QLatin1String("/")) {
        if (b == 0) { fail(DivideByZero); return 0; }
        return ints ? Num(toInt(a) / toInt(b)) : a / b;
    }
    if (op == QLatin1String("Mod")) {
        if (b == 0) { fail(DivideByZero); return 0; }
        return ints || (a == std::trunc(a) && b == std::trunc(b) && std::fabs(a) < 9e15)
            ? Num(toInt(a) % toInt(b)) : std::fmod(a, b);
    }
    if (op == QLatin1String("x^y")) {
        const Num r = std::pow(a, b);
        if (std::isnan(r)) { fail(InvalidInput); return 0; }
        return r;
    }
    if (op == QLatin1String("root")) {
        if (b == 0) { fail(InvalidInput); return 0; }
        // an odd root of a negative number is negative
        if (a < 0 && b == std::trunc(b) && std::fmod(std::fabs(b), 2) == 1)
            return -std::pow(-a, 1 / b);
        const Num r = std::pow(a, 1 / b);
        if (std::isnan(r)) { fail(InvalidInput); return 0; }
        return r;
    }
    const qint64 x = toInt(a), y = toInt(b);
    if (op == QLatin1String("And")) return Num(x & y);
    if (op == QLatin1String("Or")) return Num(x | y);
    if (op == QLatin1String("Xor")) return Num(x ^ y);
    if (op == QLatin1String("Lsh")) return y < 0 || y > 63 ? 0 : Num(qint64(quint64(x) << y));
    if (op == QLatin1String("Rsh")) return y < 0 || y > 63 ? (x < 0 ? -1 : 0) : Num(x >> y);
    return b;
}

void CalcEngine::reduceTop()
{
    const QString op = m_ops.takeLast();
    const Num b = m_values.takeLast();
    const Num a = m_values.takeLast();
    const Num r = apply(op, a, b);
    if (error())
        return;
    if (!std::isfinite(r)) {
        fail(Overflow);
        return;
    }
    m_values.append(clampInt(r));
    m_value = clampInt(r);
}

// reduces the waiting operators that go first: false on an error
bool CalcEngine::reduceWhile(int prec)
{
    while (!m_ops.isEmpty() && m_ops.last() != QLatin1String("(")) {
        const int top = precedence(m_ops.last());
        // x^y goes right to left: 2^3^2 = 2^9
        const bool rightAssoc = prec == 7 && top == 7 && m_scientific;
        if (top < prec || (rightAssoc && top == prec))
            break;
        reduceTop();
        if (error())
            return false;
    }
    return true;
}

void CalcEngine::binary(const QString &key)
{
    QString op = key;
    if (m_inv && op == QLatin1String("x^y"))
        op = QStringLiteral("root");
    if (m_inv && op == QLatin1String("Lsh"))
        op = QStringLiteral("Rsh");
    m_inv = m_hyp = false;
    // an operator right after another: it replaces it
    if (!m_haveOperand && !m_ops.isEmpty() && m_ops.last() != QLatin1String("(")) {
        m_ops.last() = op;
        return;
    }
    const Num operand = m_haveOperand ? entryValue() : m_value;
    m_values.append(clampInt(operand));
    m_entry.clear();
    m_value = clampInt(operand);
    if (!reduceWhile(precedence(op)))
        return;
    m_ops.append(op);
    m_haveOperand = false;
}

void CalcEngine::equals()
{
    if (m_ops.isEmpty()) {
        // "=" again: the last operation once more
        if (!m_lastOp.isEmpty()) {
            const Num r = apply(m_lastOp, entryValue(), m_lastOperand);
            if (!error())
                setValue(r);
        } else {
            setValue(entryValue());
        }
        m_haveOperand = true;
        return;
    }
    // "5 + =" is 5 + 5
    const Num operand = m_haveOperand ? entryValue() : m_value;
    QString last;
    for (int i = int(m_ops.size()) - 1; i >= 0 && last.isEmpty(); --i)
        if (m_ops.at(i) != QLatin1String("("))
            last = m_ops.at(i);
    m_values.append(clampInt(operand));
    while (!m_ops.isEmpty()) {
        if (m_ops.last() == QLatin1String("(")) {
            m_ops.removeLast();
            continue;
        }
        reduceTop();
        if (error())
            return;
    }
    const Num result = m_values.isEmpty() ? operand : m_values.last();
    m_values.clear();
    m_lastOp = last;
    m_lastOperand = clampInt(operand);
    setValue(result);
    m_haveOperand = true;
}

void CalcEngine::unary(const QString &f)
{
    const Num x = entryValue();
    const bool inv = m_inv, hyp = m_hyp;
    m_inv = m_hyp = false;
    Num r = 0;
    // degrees or grads to radians, and back
    const Num toRad = m_angle == QLatin1String("deg") ? Pi / 180 : m_angle == QLatin1String("grad") ? Pi / 200 : 1;
    auto bad = [this]() { fail(InvalidInput); };

    if (f == QLatin1String("sqrt")) {
        if (x < 0) return bad();
        r = std::sqrt(x);
    } else if (f == QLatin1String("%")) {
        // 200 + 5% = 210; 200 * 5% = 10
        const QString op = m_ops.isEmpty() ? QString() : m_ops.last();
        if ((op == QLatin1String("+") || op == QLatin1String("-")) && !m_values.isEmpty())
            r = m_values.last() * x / 100;
        else
            r = x / 100;
    } else if (f == QLatin1String("1/x")) {
        if (x == 0) { fail(DivideByZero); emit changed(); return; }
        r = 1 / x;
    } else if (f == QLatin1String("sin") || f == QLatin1String("cos") || f == QLatin1String("tan")) {
        if (hyp) {
            if (!inv) r = f == QLatin1String("sin") ? std::sinh(x) : f == QLatin1String("cos") ? std::cosh(x) : std::tanh(x);
            else if (f == QLatin1String("sin")) r = std::asinh(x);
            else if (f == QLatin1String("cos")) { if (x < 1) return bad(); r = std::acosh(x); }
            else { if (std::fabs(x) >= 1) return bad(); r = std::atanh(x); }
        } else if (inv) {
            if (f != QLatin1String("tan") && std::fabs(x) > 1) return bad();
            r = (f == QLatin1String("sin") ? std::asin(x) : f == QLatin1String("cos") ? std::acos(x) : std::atan(x)) / toRad;
        } else {
            const Num a = x * toRad;
            if (f == QLatin1String("tan")) {
                // tan 90 (deg), and the like: undefined
                if (std::fabs(std::cos(a)) < 1e-15) return bad();
                r = std::tan(a);
            } else {
                r = f == QLatin1String("sin") ? std::sin(a) : std::cos(a);
            }
            // sin 180 is 0, not 1.2e-16
            if (std::fabs(r) < 1e-15)
                r = 0;
            if (std::fabs(r - std::round(r)) < 1e-15)
                r = std::round(r);
        }
    } else if (f == QLatin1String("x^2")) {
        if (inv) { if (x < 0) return bad(); r = std::sqrt(x); }
        else r = x * x;
    } else if (f == QLatin1String("x^3")) {
        r = inv ? std::cbrt(x) : x * x * x;
    } else if (f == QLatin1String("ln")) {
        if (inv) r = std::exp(x);
        else { if (x <= 0) return bad(); r = std::log(x); }
    } else if (f == QLatin1String("log")) {
        if (inv) r = std::pow(10, x);
        else { if (x <= 0) return bad(); r = std::log10(x); }
    } else if (f == QLatin1String("n!")) {
        if (x < 0 && x == std::trunc(x)) return bad();
        if (x == std::trunc(x)) {
            if (x > 1754) { fail(Overflow); emit changed(); return; }
            r = 1;
            for (int i = 2; i <= int(x); ++i)
                r *= Num(i);
        } else {
            r = std::tgamma(x + 1); // n! of a fraction: the gamma function
        }
    } else if (f == QLatin1String("Int")) {
        // Inv+Int: the fractional part
        r = inv ? x - std::trunc(x) : std::trunc(x);
    } else if (f == QLatin1String("Not")) {
        r = Num(~toInt(x));
    }
    if (error())
        return;
    setValue(r);
    m_haveOperand = true;
}

void CalcEngine::press(const QString &key)
{
    if (key == QLatin1String("Clear")) {
        clearAll();
        emit changed();
        return;
    }
    // after an error only a number (or Clear) starts again
    if (error()) {
        const bool digit = key.size() == 1 && (key.at(0).isDigit() || (key.at(0) >= QLatin1Char('A') && key.at(0) <= QLatin1Char('F')));
        if (!digit && key != QLatin1String("CE") && key != QLatin1String(".") && key != QLatin1String("pi")
            && key != QLatin1String("MR"))
            return;
        clearAll();
    }

    if (key.size() == 1 && (key.at(0).isDigit() || (key.at(0) >= QLatin1Char('A') && key.at(0) <= QLatin1Char('F')))) {
        const int d = key.at(0).isDigit() ? key.at(0).digitValue() : 10 + (key.at(0).unicode() - 'A');
        if (d >= m_base)
            return; // not a digit here (9 in Oct, A in Dec)
        // the first digit after an operator (or a result) starts a new number
        if (!m_haveOperand)
            m_entry.clear();
        const QString digits = QString(m_entry).remove(QLatin1Char('-')).remove(QLatin1Char('.'));
        const int max = m_base == 10 ? MaxDecimalDigits : m_base == 16 ? 16 : m_base == 8 ? 22 : 64;
        if (digits.size() >= max && m_haveOperand)
            return;
        if (m_entry == QLatin1String("0"))
            m_entry.clear();
        else if (m_entry == QLatin1String("-0"))
            m_entry = QStringLiteral("-");
        m_entry += key;
        m_haveOperand = true;
    } else if (key == QLatin1String(".")) {
        if (m_base != 10)
            return;
        if (m_entry.isEmpty() || !m_haveOperand)
            m_entry = QStringLiteral("0.");
        else if (!m_entry.contains(QLatin1Char('.')))
            m_entry += QLatin1Char('.');
        m_haveOperand = true;
    } else if (key == QLatin1String("+/-")) {
        if (m_base != 10 && !m_entry.isEmpty()) {
            m_value = clampInt(-entryValue());
            m_entry.clear();
        } else if (!m_entry.isEmpty())
            m_entry = m_entry.startsWith(QLatin1Char('-')) ? m_entry.mid(1) : QLatin1Char('-') + m_entry;
        else
            m_value = -m_value;
        // (right after an operator: the number shown, negated, is the next one)
        m_haveOperand = true;
    } else if (key == QLatin1String("Back")) {
        if (!m_entry.isEmpty() && m_haveOperand) {
            m_entry.chop(1);
            if (m_entry.isEmpty() || m_entry == QLatin1String("-")) {
                m_entry.clear();
                m_value = 0;
            }
        }
    } else if (key == QLatin1String("CE")) {
        m_entry.clear();
        m_value = 0;
        m_haveOperand = true;
    } else if (isBinary(key)) {
        binary(key);
    } else if (key == QLatin1String("=")) {
        equals();
    } else if (key == QLatin1String("(")) {
        if (!m_scientific)
            return;
        // a bracket after a number starts afresh: the number is dropped
        m_ops.append(QStringLiteral("("));
        m_entry.clear();
        m_value = 0;
        m_haveOperand = false;
    } else if (key == QLatin1String(")")) {
        if (!m_scientific || !m_ops.contains(QStringLiteral("(")))
            return;
        const Num operand = m_haveOperand ? entryValue() : m_value;
        m_values.append(clampInt(operand));
        while (!m_ops.isEmpty() && m_ops.last() != QLatin1String("(")) {
            reduceTop();
            if (error()) {
                emit changed();
                return;
            }
        }
        m_ops.removeLast(); // the "("
        setValue(m_values.takeLast());
        m_haveOperand = true;
    } else if (isUnary(key)) {
        unary(key);
    } else if (key == QLatin1String("pi")) {
        const bool inv = m_inv;
        m_inv = m_hyp = false;
        if (m_base != 10)
            return;
        setValue(inv ? 2 * Pi : Pi);
        m_haveOperand = true;
    } else if (key == QLatin1String("MC")) {
        m_memory = 0;
    } else if (key == QLatin1String("MR")) {
        setValue(m_memory);
        m_haveOperand = true;
    } else if (key == QLatin1String("MS")) {
        m_memory = entryValue();
        m_value = entryValue();
        m_entry.clear();
        m_haveOperand = true;
    } else if (key == QLatin1String("M+")) {
        m_memory += entryValue();
        m_value = entryValue();
        m_entry.clear();
        m_haveOperand = true;
    } else {
        return;
    }
    emit changed();
}

QString CalcEngine::keyFor(const QString &text, int key, int modifiers) const
{
    const bool ctrl = modifiers & Qt::ControlModifier;
    if (ctrl) {
        switch (key) {
        case Qt::Key_M: return QStringLiteral("MS");
        case Qt::Key_R: return QStringLiteral("MR");
        case Qt::Key_L: return QStringLiteral("MC");
        case Qt::Key_P: return QStringLiteral("M+");
        default: return {};
        }
    }
    switch (key) {
    case Qt::Key_Return: case Qt::Key_Enter: return QStringLiteral("=");
    case Qt::Key_Escape: return QStringLiteral("Clear");
    case Qt::Key_Delete: return QStringLiteral("CE");
    case Qt::Key_Backspace: return QStringLiteral("Back");
    case Qt::Key_F9: return QStringLiteral("+/-");
    default: break;
    }
    if (text.size() != 1)
        return {};
    const QChar c = text.at(0);
    if (c.isDigit())
        return QString(c);
    if (c >= QLatin1Char('a') && c <= QLatin1Char('f') && m_base == 16)
        return QString(c.toUpper());
    if (c >= QLatin1Char('A') && c <= QLatin1Char('F'))
        return QString(c);
    switch (c.unicode()) {
    case '.': case ',': return QStringLiteral(".");
    case '+': return QStringLiteral("+");
    case '-': return QStringLiteral("-");
    case '*': return QStringLiteral("*");
    case '/': return QStringLiteral("/");
    case '=': return QStringLiteral("=");
    case '%': return QStringLiteral("%");
    case '@': return QStringLiteral("sqrt");
    case 'r': return QStringLiteral("1/x");
    case '(': return QStringLiteral("(");
    case ')': return QStringLiteral(")");
    case '^': case 'y': return QStringLiteral("x^y");
    case 's': return QStringLiteral("sin");
    case 'o': return QStringLiteral("cos");
    case 't': return QStringLiteral("tan");
    case 'n': return QStringLiteral("ln");
    case 'l': return QStringLiteral("log");
    case '!': return QStringLiteral("n!");
    case 'p': return QStringLiteral("pi");
    case 'q': return QStringLiteral("x^2");
    case '#': return QStringLiteral("x^3");
    case '&': return QStringLiteral("And");
    case '|': return QStringLiteral("Or");
    case '~': return QStringLiteral("Not");
    case '<': return QStringLiteral("Lsh");
    case ';': return QStringLiteral("Int");
    default: return {};
    }
}

void CalcEngine::paste(const QString &text)
{
    for (const QChar c : text) {
        if (c.isSpace())
            continue;
        const QString k = c == QLatin1Char('\n') ? QStringLiteral("=") : keyFor(QString(c), 0, 0);
        // (in a paste, letters are only hex digits)
        if (k.isEmpty() || (c.isLetter() && !(k.size() == 1 && k.at(0) >= QLatin1Char('A') && k.at(0) <= QLatin1Char('F'))))
            continue;
        press(k);
    }
}
