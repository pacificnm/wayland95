#pragma once

#include <QString>
#include <functional>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// A field's Validation Rule: one picked from a set the Data Designer offers
// for the field's type (Letters only, UK postcode, In the past, More than
// another field...), some with a value of their own (the text it has to
// start with, how many days, which field). The field keeps it as
//
//   { validator: "starts_with", validatorArg: "AB", message: "..." }
//
// "expression" is the rule written out (the field's `rule`, as before
// there was a list): a field with a `rule` and no `validator` has that.
// Each rule has a message of its own, said when the field's Validation
// Text is blank.
namespace FieldRules {

// what a rule's value is
enum class Arg { None, Number, Count, Text, List, Field, Expression };

struct Rule
{
    const char *id;
    const char *label;
    const char *types;     // the field types it's for, space-separated
    Arg arg;
    const char *argLabel;  // the value's row in the field's properties
    const char *argHint;
    const char *message;   // %1 the field's caption, %2 the value
};

// the rule with this id (nullptr if there's none)
const Rule *find(const QString &id);
// the field's rule: its validator, or "expression" for a `rule` alone
QString ruleOf(const QVariantMap &field);

// the rules for a field type, for the Data Designer:
// [{ id, label, arg, argLabel, argHint, argTypes, message }]
// (argTypes: the types of the fields a Field value can name)
QVariantList forType(const QString &type);

// what's said when the field's rule isn't met and it has no Validation
// Text of its own ("" if it has no rule). `table` gives the other field's
// caption.
QString defaultMessage(const QVariantMap &table, const QVariantMap &field);
// the rule in a few words, for the Data Designer's list of fields
QString summary(const QVariantMap &table, const QVariantMap &field);

// The rule against a value. `value` is as it will be stored (or invalid
// if the field is empty; `blank` is true then, and for a Yes/No that's
// No). `valueOf` gives another field's value in the record, as it will
// be stored (invalid if it's empty or can't be). Returns Pass, Fail (with
// the rule's own message in `problem`), or Broken with what's wrong with
// the rule itself. ("expression" passes: AppDatabase runs those.)
enum class Result { Pass, Fail, Broken };
struct Check
{
    Result result = Result::Pass;
    QString problem;
};
Check check(const QVariantMap &table, const QVariantMap &field, const QVariant &value, bool blank,
            const std::function<QVariant(const QVariantMap &field)> &valueOf);

} // namespace FieldRules
