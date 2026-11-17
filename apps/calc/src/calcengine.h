#pragma once

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

// The Calculator's arithmetic, key by key, as a pocket calculator does it.
//
// press() takes one key: digits 0-9 and A-F, "." "+/-", the operators
// "+" "-" "*" "/" "Mod" "And" "Or" "Xor" "Lsh" "x^y", "=" "(" ")", the
// functions "sqrt" "%" "1/x" "sin" "cos" "tan" "x^2" "x^3" "ln" "log" "n!"
// "Int" "Not" "pi", the memory "MC" "MR" "MS" "M+", and "Clear" (C) "CE"
// "Back". ("C" is the hex digit.)
//
// Standard works left to right as you type (2 + 3 * 4 = 20, like Windows'
// Standard calculator); Scientific follows the usual order (x^y, then * /
// Mod, then + -, then Lsh, And, Xor, Or: 2 + 3 * 4 = 14). "=" again
// repeats the last operation. Inv and Hyp turn the next function into its
// inverse (asin, y-th root, sqrt, e^x...) or hyperbolic one.
//
// In Hex, Oct and Bin the numbers are 64-bit integers. Exposed to QML as
// `CalcEngine` (one per window).
// the numbers: long double (x87 extended: 64-bit mantissa) holds 16-digit
// whole numbers exactly, as a calculator must (a double stops at 2^53)
using Num = long double;

class CalcEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString display READ display NOTIFY changed)
    Q_PROPERTY(bool error READ error NOTIFY changed)
    Q_PROPERTY(bool memory READ hasMemory NOTIFY changed)
    // the open brackets: "(=2" in Scientific
    Q_PROPERTY(int openBrackets READ openBrackets NOTIFY changed)
    Q_PROPERTY(bool scientific READ scientific WRITE setScientific NOTIFY changed)
    Q_PROPERTY(int base READ base WRITE setBase NOTIFY changed)            // 10, 16, 8, 2
    Q_PROPERTY(QString angle READ angle WRITE setAngle NOTIFY changed)     // deg, rad, grad
    Q_PROPERTY(bool inv READ inv WRITE setInv NOTIFY changed)
    Q_PROPERTY(bool hyp READ hyp WRITE setHyp NOTIFY changed)
    Q_PROPERTY(bool grouping READ grouping WRITE setGrouping NOTIFY changed)

public:
    explicit CalcEngine(QObject *parent = nullptr);

    QString display() const;
    bool error() const { return !m_error.isEmpty(); }
    bool hasMemory() const { return m_memory != 0; }
    int openBrackets() const;
    bool scientific() const { return m_scientific; }
    void setScientific(bool on);
    int base() const { return m_base; }
    void setBase(int base);
    QString angle() const { return m_angle; }
    void setAngle(const QString &angle);
    bool inv() const { return m_inv; }
    void setInv(bool on);
    bool hyp() const { return m_hyp; }
    void setHyp(bool on);
    bool grouping() const { return m_grouping; }
    void setGrouping(bool on);

    Q_INVOKABLE void press(const QString &key);
    // a keyboard key (its text and Qt key code) as the calculator's key, or ""
    Q_INVOKABLE QString keyFor(const QString &text, int key, int modifiers) const;
    // what Copy copies (the number, without grouping)
    Q_INVOKABLE QString copyText() const;
    // types what's pasted: digits, operators, = and the rest (other
    // characters are left out)
    Q_INVOKABLE void paste(const QString &text);

    Num value() const { return m_value; }

signals:
    void changed();

private:
    void clearAll();
    void setValue(Num v);
    Num entryValue() const;
    void binary(const QString &op);
    void reduceTop();
    bool reduceWhile(int precedence);
    void equals();
    void unary(const QString &f);
    Num apply(const QString &op, Num a, Num b);
    int precedence(const QString &op) const;
    QString format(Num v) const;
    Num clampInt(Num v) const;
    void fail(const QString &message);

    QString m_entry;           // digits being typed ("" when showing m_value)
    Num m_value = 0;        // what's shown when not typing
    bool m_haveOperand = false; // a number since the last operator
    QList<Num> m_values;
    QStringList m_ops;         // binary operators and "("
    QString m_lastOp;          // for "=" again
    Num m_lastOperand = 0;
    Num m_memory = 0;
    QString m_error;
    bool m_scientific = false;
    int m_base = 10;
    QString m_angle = QStringLiteral("deg");
    bool m_inv = false, m_hyp = false;
    bool m_grouping = false;
};
