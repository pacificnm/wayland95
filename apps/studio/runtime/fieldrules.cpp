#include "fieldrules.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QTime>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace FieldRules {

namespace {

// the field types, by what they hold
#define TEXT "text memo"
#define TEXTISH "text memo email url phone"
#define NUMBERS "integer decimal float currency"
#define FRACTIONS "decimal float currency"
#define DATES "date datetime"
#define TIMES "time datetime"
#define WHENS "date datetime time"
#define ALL "text memo email url phone integer decimal float currency boolean date time datetime choice lookup"

const Rule kRules[] = {
    // --- characters
    { "letters", "Letters only", TEXT, Arg::None, "", "", "%1 can only have letters in it." },
    { "letters_spaces", "Letters and spaces only", TEXT, Arg::None, "", "", "%1 can only have letters and spaces in it." },
    { "person_name", "A person's name (letters, spaces, - ' .)", TEXT, Arg::None, "", "",
      "%1 can only have letters, spaces, hyphens, apostrophes and full stops in it." },
    { "digits", "Digits only (0-9)", TEXTISH, Arg::None, "", "", "%1 can only have digits (0-9) in it." },
    { "alphanumeric", "Letters and digits only", TEXT, Arg::None, "", "", "%1 can only have letters and digits in it." },
    { "alphanumeric_spaces", "Letters, digits and spaces only", TEXT, Arg::None, "", "",
      "%1 can only have letters, digits and spaces in it." },
    { "no_spaces", "No spaces", TEXTISH, Arg::None, "", "", "%1 can't have spaces in it." },
    { "ascii", "Plain characters only (ASCII)", TEXTISH, Arg::None, "", "",
      "%1 can only have plain characters in it (no accents or symbols beyond ASCII)." },
    { "uppercase", "All capitals", TEXT, Arg::None, "", "", "%1 has to be in capitals." },
    { "lowercase", "All lower case", TEXTISH, Arg::None, "", "", "%1 has to be in lower case." },
    { "capitalized", "Starts with a capital letter", TEXT, Arg::None, "", "", "%1 has to start with a capital letter." },
    { "single_line", "One line only", "memo", Arg::None, "", "", "%1 has to be on one line." },
    { "no_html", "No HTML tags", TEXT, Arg::None, "", "", "%1 can't have HTML tags in it." },

    // --- what's in it
    { "starts_with", "Starts with…", TEXTISH, Arg::Text, "Starts With", "The text the value has to start with (capitals count).",
      "%1 has to start with \"%2\"." },
    { "ends_with", "Ends with…", TEXTISH, Arg::Text, "Ends With",
      "The text the value has to end with (capitals count), like @example.com.", "%1 has to end with \"%2\"." },
    { "contains", "Contains…", TEXTISH, Arg::Text, "Has To Contain", "The text that has to be somewhere in the value (capitals count).",
      "%1 has to have \"%2\" in it." },
    { "not_contains", "Doesn't contain…", TEXTISH, Arg::Text, "Can't Contain",
      "The text that can't be anywhere in the value (capitals don't count).", "%1 can't have \"%2\" in it." },
    { "one_of", "One of a list…", TEXTISH, Arg::List, "Allowed Values", "The values allowed, one on each line (capitals count).",
      "%1 has to be one of: %2." },
    { "not_one_of", "Not one of a list…", TEXTISH, Arg::List, "Refused Values",
      "The values that aren't allowed, one on each line (capitals don't count).", "%1 can't be any of: %2." },
    { "exact_length", "Exactly N characters", TEXTISH, Arg::Count, "Length", "How many characters the value has to have.",
      "%1 has to be exactly %2 characters long." },
    { "min_words", "At least N words", TEXT, Arg::Count, "Fewest Words", "The fewest words allowed.",
      "%1 needs at least %2 words." },
    { "max_words", "At most N words", TEXT, Arg::Count, "Most Words", "The most words allowed.", "%1 can have at most %2 words." },

    // --- internet
    { "email", "E-mail address", TEXT, Arg::None, "", "", "%1 has to be an e-mail address, like name@example.com." },
    { "web_url", "Web address (http:// or https://)", "text memo url", Arg::None, "", "",
      "%1 has to be a web address starting with http:// or https://." },
    { "https_url", "Secure web address (https://)", "text memo url", Arg::None, "", "",
      "%1 has to be a web address starting with https://." },
    { "domain", "Domain name", TEXT, Arg::None, "", "", "%1 has to be a domain name, like example.com." },
    { "ipv4", "IPv4 address", TEXT, Arg::None, "", "", "%1 has to be an IPv4 address, like 192.168.1.10." },
    { "ipv6", "IPv6 address", TEXT, Arg::None, "", "", "%1 has to be an IPv6 address, like 2001:db8::1." },
    { "ip", "IP address (v4 or v6)", TEXT, Arg::None, "", "", "%1 has to be an IP address, like 192.168.1.10 or 2001:db8::1." },
    { "mac", "MAC address", TEXT, Arg::None, "", "", "%1 has to be a MAC address, like 00:1A:2B:3C:4D:5E." },
    { "username", "User name (3-32 letters, digits, . _ -)", TEXT, Arg::None, "", "",
      "%1 has to be 3 to 32 letters, digits, dots, dashes or underscores, starting with a letter or digit." },
    { "strong_password", "Strong password", TEXT, Arg::None, "", "",
      "%1 needs at least 8 characters, with a capital, a small letter, a digit and a symbol." },
    { "slug", "Slug (lower-case words with dashes)", TEXT, Arg::None, "", "",
      "%1 has to be lower-case letters and digits with dashes between words, like my-first-post." },
    { "identifier", "Identifier (letters, digits, _)", TEXT, Arg::None, "", "",
      "%1 has to be letters, digits and _, not starting with a digit." },
    { "uuid", "UUID", TEXT, Arg::None, "", "", "%1 has to be a UUID, like 123e4567-e89b-12d3-a456-426614174000." },
    { "hex", "Hexadecimal", TEXT, Arg::None, "", "", "%1 has to be a hexadecimal number (0-9, A-F)." },
    { "hex_color", "Colour code (#RRGGBB)", TEXT, Arg::None, "", "", "%1 has to be a colour code, like #1A2B3C." },
    { "json", "JSON", TEXT, Arg::None, "", "", "%1 has to be valid JSON." },

    // --- telephones and places
    { "phone_nanp", "Phone number (US and Canada)", "text phone", Arg::None, "", "",
      "%1 has to be a US or Canadian phone number, like (212) 555-0123." },
    { "phone_e164", "Phone number (international, +country code)", "text phone", Arg::None, "", "",
      "%1 has to start with + and the country code, like +44 20 7946 0958." },
    { "zip_us", "ZIP code (US)", TEXT, Arg::None, "", "", "%1 has to be a ZIP code, like 90210 or 90210-1234." },
    { "postal_ca", "Postal code (Canada)", TEXT, Arg::None, "", "", "%1 has to be a Canadian postal code, like K1A 0B1." },
    { "postcode_uk", "Postcode (UK)", TEXT, Arg::None, "", "", "%1 has to be a UK postcode, like SW1A 1AA." },
    { "postcode_5", "Postal code (5 digits: DE, FR, IT, ES...)", TEXT, Arg::None, "", "", "%1 has to be a 5-digit postal code." },
    { "postcode_4", "Postcode (4 digits: AU, NZ, AT, CH, BE...)", TEXT, Arg::None, "", "", "%1 has to be a 4-digit postcode." },
    { "country_code", "Country code (ISO, 2 letters)", TEXT, Arg::None, "", "", "%1 has to be a two-letter country code, like US or GB." },
    { "language_code", "Language code (ISO, 2 or 3 letters)", TEXT, Arg::None, "", "",
      "%1 has to be a language code, like en or fr." },
    { "currency_code", "Currency code (ISO, 3 letters)", TEXT, Arg::None, "", "", "%1 has to be a currency code, like USD or EUR." },

    // --- numbers with a check in them
    { "credit_card", "Card number (with its check digit)", TEXT, Arg::None, "", "", "%1 has to be a card number." },
    { "iban", "IBAN (international bank account)", TEXT, Arg::None, "", "",
      "%1 has to be an IBAN, like GB82 WEST 1234 5698 7654 32." },
    { "bic", "SWIFT/BIC code", TEXT, Arg::None, "", "", "%1 has to be a SWIFT/BIC code, like DEUTDEFF." },
    { "routing_us", "Bank routing number (US ABA)", TEXT, Arg::None, "", "", "%1 has to be a 9-digit bank routing number." },
    { "ssn_us", "Social Security number (US)", TEXT, Arg::None, "", "", "%1 has to be a Social Security number, like 123-45-6789." },
    { "ein_us", "Employer ID number (US EIN)", TEXT, Arg::None, "", "", "%1 has to be an EIN, like 12-3456789." },
    { "isbn", "ISBN (10 or 13 digits)", TEXT, Arg::None, "", "", "%1 has to be an ISBN, like 978-0-306-40615-7." },
    { "barcode", "Barcode (EAN-8, UPC-A, EAN-13, GTIN-14)", TEXT, Arg::None, "", "", "%1 has to be a barcode number." },
    { "vin", "Vehicle identification number (VIN)", TEXT, Arg::None, "", "", "%1 has to be a 17-character VIN." },

    // --- numbers
    { "positive", "More than zero", NUMBERS, Arg::None, "", "", "%1 has to be more than 0." },
    { "not_negative", "Zero or more", NUMBERS, Arg::None, "", "", "%1 can't be less than 0." },
    { "negative", "Less than zero", NUMBERS, Arg::None, "", "", "%1 has to be less than 0." },
    { "not_zero", "Not zero", NUMBERS, Arg::None, "", "", "%1 can't be 0." },
    { "greater_than", "More than…", NUMBERS, Arg::Number, "More Than", "The value has to be more than this (not equal to it).",
      "%1 has to be more than %2." },
    { "less_than", "Less than…", NUMBERS, Arg::Number, "Less Than", "The value has to be less than this (not equal to it).",
      "%1 has to be less than %2." },
    { "not_equal", "Not equal to…", NUMBERS, Arg::Number, "Can't Be", "The one value that isn't allowed.", "%1 can't be %2." },
    { "multiple_of", "A multiple of…", NUMBERS, Arg::Number, "Multiple Of", "The step the value goes in, like 5 or 0.25.",
      "%1 has to be a multiple of %2." },
    { "whole", "A whole number", FRACTIONS, Arg::None, "", "", "%1 has to be a whole number." },
    { "max_decimals", "At most N decimal places", FRACTIONS, Arg::Count, "Decimal Places",
      "The most digits allowed after the point.", "%1 can have at most %2 decimal places." },
    { "even", "An even number", "integer", Arg::None, "", "", "%1 has to be an even number." },
    { "odd", "An odd number", "integer", Arg::None, "", "", "%1 has to be an odd number." },
    { "percentage", "A percentage (0 to 100)", NUMBERS, Arg::None, "", "", "%1 has to be from 0 to 100." },
    { "fraction", "A fraction (0 to 1)", FRACTIONS, Arg::None, "", "", "%1 has to be from 0 to 1." },
    { "latitude", "Latitude (-90 to 90)", FRACTIONS, Arg::None, "", "", "%1 has to be a latitude, from -90 to 90." },
    { "longitude", "Longitude (-180 to 180)", FRACTIONS, Arg::None, "", "", "%1 has to be a longitude, from -180 to 180." },
    { "year", "A year (four digits)", "integer", Arg::None, "", "", "%1 has to be a four-digit year." },
    { "month", "A month (1 to 12)", "integer", Arg::None, "", "", "%1 has to be a month, from 1 to 12." },
    { "day_of_month", "A day of the month (1 to 31)", "integer", Arg::None, "", "", "%1 has to be a day of the month, from 1 to 31." },
    { "port", "A network port (1 to 65535)", "integer", Arg::None, "", "", "%1 has to be a port number, from 1 to 65535." },

    // --- dates
    { "past", "In the past", DATES, Arg::None, "", "", "%1 has to be in the past." },
    { "not_future", "Not in the future", DATES, Arg::None, "", "", "%1 can't be in the future." },
    { "future", "In the future", DATES, Arg::None, "", "", "%1 has to be in the future." },
    { "not_past", "Not in the past", DATES, Arg::None, "", "", "%1 can't be in the past." },
    { "today", "Today", "date", Arg::None, "", "", "%1 has to be today." },
    { "within_past_days", "Within the last N days", DATES, Arg::Count, "Days", "How many days back the value can be.",
      "%1 has to be within the last %2 days." },
    { "within_next_days", "Within the next N days", DATES, Arg::Count, "Days", "How many days ahead the value can be.",
      "%1 has to be within the next %2 days." },
    { "min_age", "At least N years ago (an age)", DATES, Arg::Count, "Years",
      "How many whole years ago the date has to be, at least: 18 on a birth date is 18 or older.",
      "%1 has to be at least %2 years ago." },
    { "max_age", "At most N years ago (an age)", DATES, Arg::Count, "Years",
      "How many whole years ago the date can be, at most: 65 on a birth date is 65 or younger.",
      "%1 can't be more than %2 years ago." },
    { "weekday", "A weekday (Monday to Friday)", DATES, Arg::None, "", "", "%1 has to be a weekday (Monday to Friday)." },
    { "weekend", "A weekend (Saturday or Sunday)", DATES, Arg::None, "", "", "%1 has to be on a Saturday or Sunday." },
    { "first_of_month", "The first day of a month", DATES, Arg::None, "", "", "%1 has to be the first day of a month." },
    { "last_of_month", "The last day of a month", DATES, Arg::None, "", "", "%1 has to be the last day of a month." },

    // --- times
    { "whole_hour", "On the hour", TIMES, Arg::None, "", "", "%1 has to be on the hour, like 14:00." },
    { "minute_step", "Every N minutes", TIMES, Arg::Count, "Minutes", "The step times go in: 15 allows :00, :15, :30 and :45.",
      "%1 has to be on a %2-minute step." },
    { "working_hours", "Working hours (9:00 to 17:00)", TIMES, Arg::None, "", "", "%1 has to be between 9:00 and 17:00." },

    // --- another field
    { "same_as", "The same as another field", ALL, Arg::Field, "Other Field", "The field this has to match.",
      "%1 has to be the same as %2." },
    { "different_from", "Different from another field", ALL, Arg::Field, "Other Field", "The field this can't be the same as.",
      "%1 can't be the same as %2." },
    { "more_than_field", "More than another field", NUMBERS, Arg::Field, "Other Field", "The field this has to be more than.",
      "%1 has to be more than %2." },
    { "at_least_field", "At least another field", NUMBERS, Arg::Field, "Other Field", "The field this can't be less than.",
      "%1 can't be less than %2." },
    { "less_than_field", "Less than another field", NUMBERS, Arg::Field, "Other Field", "The field this has to be less than.",
      "%1 has to be less than %2." },
    { "at_most_field", "At most another field", NUMBERS, Arg::Field, "Other Field", "The field this can't be more than.",
      "%1 can't be more than %2." },
    { "after_field", "After another field", WHENS, Arg::Field, "Other Field", "The field this has to come after.",
      "%1 has to be after %2." },
    { "on_or_after_field", "On or after another field", WHENS, Arg::Field, "Other Field", "The field this can't come before.",
      "%1 can't be before %2." },
    { "before_field", "Before another field", WHENS, Arg::Field, "Other Field", "The field this has to come before.",
      "%1 has to be before %2." },
    { "on_or_before_field", "On or before another field", WHENS, Arg::Field, "Other Field", "The field this can't come after.",
      "%1 can't be after %2." },
    { "required_if", "Required when another field is filled in", ALL, Arg::Field, "When Filled In",
      "This field is required while that one has a value (or is Yes).", "%1 is required when %2 is filled in." },
    { "required_unless", "Required when another field is empty", ALL, Arg::Field, "When Empty",
      "This field is required while that one is empty (or No): one of the two has to be given.",
      "%1 is required when %2 is empty." },
    { "only_if", "Only when another field is filled in", ALL, Arg::Field, "Only With",
      "This field has to be left empty (or No) unless that one has a value (or is Yes).",
      "%1 has to be left empty unless %2 is filled in." },

    // --- written out
    { "expression", "Custom expression…", ALL, Arg::Expression, "Expression",
      "An expression that has to be true. value is this field, record the whole record: value <= 50 || record.Kind === \"Family\"",
      "%1 isn't valid." },
};

bool hasType(const Rule &rule, const QString &type)
{
    return QString::fromLatin1(rule.types).split(QLatin1Char(' ')).contains(type);
}

// the types a Field value can name: those that compare with this one
QStringList fieldArgTypes(const Rule &rule, const QString &type)
{
    const QString id = QLatin1String(rule.id);
    if (id == QLatin1String("required_if") || id == QLatin1String("required_unless") || id == QLatin1String("only_if"))
        return QString::fromLatin1(ALL).split(QLatin1Char(' '));
    const QStringList families[] = {
        QString::fromLatin1(NUMBERS).split(QLatin1Char(' ')),
        QStringList{ QStringLiteral("date"), QStringLiteral("datetime") },
        QStringList{ QStringLiteral("text"), QStringLiteral("memo"), QStringLiteral("email"), QStringLiteral("url"),
                     QStringLiteral("phone"), QStringLiteral("choice") },
    };
    for (const QStringList &family : families) {
        if (family.contains(type))
            return family;
    }
    return { type };
}

QString caption(const QVariantMap &field)
{
    const QString l = field.value(QStringLiteral("label")).toString().trimmed();
    return l.isEmpty() ? field.value(QStringLiteral("name")).toString() : l;
}

QVariantMap fieldNamed(const QVariantMap &table, const QString &name)
{
    for (const QVariant &f : table.value(QStringLiteral("fields")).toList()) {
        const QVariantMap field = f.toMap();
        if (field.value(QStringLiteral("name")).toString() == name)
            return field;
    }
    return {};
}

QStringList listArg(const QVariant &v)
{
    QStringList out;
    const QStringList items = v.typeId() == QMetaType::QString ? v.toString().split(QLatin1Char('\n')) : v.toStringList();
    for (const QString &s : items) {
        if (!s.trimmed().isEmpty())
            out << s.trimmed();
    }
    return out;
}

QString numberText(double d)
{
    return QString::number(d, 'g', 15);
}

// the rule's value as the message says it ("" if it has none)
QString argText(const Rule &rule, const QVariantMap &table, const QVariantMap &field)
{
    const QVariant v = field.value(QStringLiteral("validatorArg"));
    switch (rule.arg) {
    case Arg::List:
        return listArg(v).join(QLatin1String(", "));
    case Arg::Field: {
        const QVariantMap other = fieldNamed(table, v.toString());
        return other.isEmpty() ? v.toString() : caption(other);
    }
    case Arg::Number:
    case Arg::Count: {
        bool ok = false;
        const double d = v.toDouble(&ok);
        return ok && !v.toString().trimmed().isEmpty() ? numberText(d) : QString();
    }
    case Arg::Text:
        return v.toString();
    default:
        return QString();
    }
}

// %1 and %2 in one pass (so a caption with a % in it stays as it is)
QString fill(const char *text, const QString &one, const QString &two)
{
    const QString t = QString::fromUtf8(text);
    QString out;
    for (int i = 0; i < t.size(); ++i) {
        if (t.at(i) == QLatin1Char('%') && i + 1 < t.size() && (t.at(i + 1) == QLatin1Char('1') || t.at(i + 1) == QLatin1Char('2'))) {
            out += t.at(i + 1) == QLatin1Char('1') ? one : two;
            ++i;
        } else {
            out += t.at(i);
        }
    }
    return out;
}

// a rule's message, worded for the fields it's about
QString message(const Rule &rule, const QVariantMap &table, const QVariantMap &field, const QString &one)
{
    QString arg = argText(rule, table, field);
    if (arg.isEmpty())
        arg = QStringLiteral("…");
    QString out = fill(rule.message, one, arg);
    const QString id = QLatin1String(rule.id);
    if (id == QLatin1String("required_if") || id == QLatin1String("required_unless") || id == QLatin1String("only_if")) {
        // a Yes/No is "filled in" when it's Yes
        const QVariantMap other = fieldNamed(table, field.value(QStringLiteral("validatorArg")).toString());
        if (other.value(QStringLiteral("type")).toString() == QLatin1String("boolean")) {
            out.replace(QLatin1String(" is filled in."), QLatin1String(" is Yes."));
            out.replace(QLatin1String(" is empty."), QLatin1String(" is No."));
        }
        if (field.value(QStringLiteral("type")).toString() == QLatin1String("boolean")) {
            out.replace(QLatin1String(" is required "), QLatin1String(" has to be Yes "));
            out.replace(QLatin1String(" has to be left empty "), QLatin1String(" has to be No "));
        }
    }
    return out;
}

// --- the checks that want more than a pattern -----------------------------------

bool matches(const QString &text, const char *pattern, bool caseless = false)
{
    static QHash<QString, QRegularExpression> cache;
    const QString key = QString::fromUtf8(pattern) + (caseless ? QStringLiteral("/i") : QString());
    auto it = cache.find(key);
    if (it == cache.end()) {
        QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
        if (caseless)
            options |= QRegularExpression::CaseInsensitiveOption;
        it = cache.insert(key, QRegularExpression(QRegularExpression::anchoredPattern(QString::fromUtf8(pattern)), options));
    }
    return it->match(text).hasMatch();
}

QString digitsOf(const QString &text, const char *allowed)
{
    QString out;
    for (QChar c : text) {
        if (c.isDigit())
            out += c;
        else if (!QString::fromLatin1(allowed).contains(c))
            return QString();
    }
    return out;
}

bool isIpv4(const QString &s)
{
    const QStringList parts = s.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return false;
    for (const QString &p : parts) {
        if (p.isEmpty() || p.size() > 3 || !matches(p, "[0-9]+") || p.toInt() > 255)
            return false;
    }
    return true;
}

// groups of 1-4 hex digits; the last can be an IPv4 address (two groups)
bool ipv6Groups(const QString &part, bool lastMayBeV4, int *count)
{
    *count = 0;
    if (part.isEmpty())
        return true;
    const QStringList groups = part.split(QLatin1Char(':'));
    for (int i = 0; i < groups.size(); ++i) {
        if (lastMayBeV4 && i == groups.size() - 1 && groups[i].contains(QLatin1Char('.'))) {
            if (!isIpv4(groups[i]))
                return false;
            *count += 2;
        } else if (matches(groups[i], "[0-9A-Fa-f]{1,4}")) {
            *count += 1;
        } else {
            return false;
        }
    }
    return true;
}

bool isIpv6(const QString &s)
{
    const int gap = s.indexOf(QLatin1String("::"));
    if (gap >= 0 && s.lastIndexOf(QLatin1String("::")) != gap)
        return false;
    int head = 0, tail = 0;
    if (gap < 0)
        return ipv6Groups(s, true, &head) && head == 8;
    return ipv6Groups(s.left(gap), false, &head) && ipv6Groups(s.mid(gap + 2), true, &tail) && head + tail <= 7;
}

bool luhn(const QString &digits)
{
    int sum = 0;
    bool twice = false;
    for (int i = digits.size() - 1; i >= 0; --i) {
        int d = digits.at(i).digitValue();
        if (twice && (d *= 2) > 9)
            d -= 9;
        sum += d;
        twice = !twice;
    }
    return sum % 10 == 0;
}

// EAN-8, UPC-A, EAN-13, GTIN-14 (and ISBN-13)
bool gtin(const QString &digits)
{
    int sum = 0;
    for (int i = digits.size() - 2, w = 3; i >= 0; --i, w = 4 - w)
        sum += digits.at(i).digitValue() * w;
    return (10 - sum % 10) % 10 == digits.back().digitValue();
}

bool isIsbn(const QString &text)
{
    QString s = text;
    s.remove(QLatin1Char('-')).remove(QLatin1Char(' '));
    if (matches(s, "97[89][0-9]{10}"))
        return gtin(s);
    if (!matches(s, "[0-9]{9}[0-9Xx]"))
        return false;
    int sum = 0;
    for (int i = 0; i < 10; ++i)
        sum += (10 - i) * (s.at(i).toUpper() == QLatin1Char('X') ? 10 : s.at(i).digitValue());
    return sum % 11 == 0;
}

bool isIban(const QString &text)
{
    QString s = text.toUpper();
    s.remove(QLatin1Char(' '));
    if (!matches(s, "[A-Z]{2}[0-9]{2}[A-Z0-9]{11,30}"))
        return false;
    s = s.mid(4) + s.left(4);
    int rest = 0;
    for (QChar c : s) {
        const int v = c.isDigit() ? c.digitValue() : c.unicode() - 'A' + 10;
        rest = (v > 9 ? rest * 100 + v : rest * 10 + v) % 97;
    }
    return rest == 1;
}

bool isRouting(const QString &s)
{
    if (!matches(s, "[0-9]{9}"))
        return false;
    auto d = [&](int i) { return s.at(i).digitValue(); };
    return (3 * (d(0) + d(3) + d(6)) + 7 * (d(1) + d(4) + d(7)) + d(2) + d(5) + d(8)) % 10 == 0;
}

bool isCurrency(const QString &code)
{
    static QSet<QString> codes;
    if (codes.isEmpty()) {
        const auto locales = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyTerritory);
        for (const QLocale &l : locales)
            codes.insert(l.currencySymbol(QLocale::CurrencyIsoCode));
        codes.remove(QString());
    }
    return codes.contains(code);
}

int wholeYearsSince(const QDate &d)
{
    const QDate today = QDate::currentDate();
    int years = today.year() - d.year();
    if (d.addYears(years) > today)
        --years;
    return years;
}

// the text rules: true if `s` passes
bool textPasses(const QString &id, const QString &s, const QVariant &arg)
{
    if (id == QLatin1String("letters")) return matches(s, "[\\p{L}\\p{M}]+");
    if (id == QLatin1String("letters_spaces")) return matches(s, "[\\p{L}\\p{M} ]+");
    if (id == QLatin1String("person_name")) return matches(s, "[\\p{L}\\p{M}][\\p{L}\\p{M} '’.-]*");
    if (id == QLatin1String("digits")) return matches(s, "[0-9]+");
    if (id == QLatin1String("alphanumeric")) return matches(s, "[\\p{L}\\p{M}0-9]+");
    if (id == QLatin1String("alphanumeric_spaces")) return matches(s, "[\\p{L}\\p{M}0-9 ]+");
    if (id == QLatin1String("no_spaces")) return !matches(s, "(?s).*\\s.*");
    if (id == QLatin1String("ascii")) return matches(s, "[\\x20-\\x7E\\t\\r\\n]*");
    if (id == QLatin1String("uppercase")) return s == s.toUpper();
    if (id == QLatin1String("lowercase")) return s == s.toLower();
    if (id == QLatin1String("capitalized")) return s.at(0).isUpper();
    if (id == QLatin1String("single_line")) return !s.contains(QLatin1Char('\n'));
    if (id == QLatin1String("no_html")) return !matches(s, "(?s).*<\\s*/?\\s*[A-Za-z!][^>]*>.*");

    if (id == QLatin1String("starts_with")) return s.startsWith(arg.toString());
    if (id == QLatin1String("ends_with")) return s.endsWith(arg.toString());
    if (id == QLatin1String("contains")) return s.contains(arg.toString());
    if (id == QLatin1String("not_contains")) return !s.contains(arg.toString(), Qt::CaseInsensitive);
    if (id == QLatin1String("one_of")) return listArg(arg).contains(s);
    if (id == QLatin1String("not_one_of")) return !listArg(arg).contains(s, Qt::CaseInsensitive);
    if (id == QLatin1String("exact_length")) return s.size() == arg.toInt();
    if (id == QLatin1String("min_words") || id == QLatin1String("max_words")) {
        static const QRegularExpression space(QStringLiteral("\\s+"));
        const qsizetype words = s.split(space, Qt::SkipEmptyParts).size();
        return id == QLatin1String("min_words") ? words >= arg.toInt() : words <= arg.toInt();
    }

    if (id == QLatin1String("email")) return matches(s, "[^@\\s]+@[^@\\s.]+(\\.[^@\\s.]+)+");
    if (id == QLatin1String("web_url")) return matches(s, "https?://[^\\s/:?#]+(:[0-9]+)?([/?#]\\S*)?", true);
    if (id == QLatin1String("https_url")) return matches(s, "https://[^\\s/:?#]+(:[0-9]+)?([/?#]\\S*)?", true);
    if (id == QLatin1String("domain"))
        return s.size() <= 253 && matches(s, "([A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?\\.)+[A-Za-z][A-Za-z0-9-]{0,61}[A-Za-z0-9]");
    if (id == QLatin1String("ipv4")) return isIpv4(s);
    if (id == QLatin1String("ipv6")) return isIpv6(s);
    if (id == QLatin1String("ip")) return isIpv4(s) || isIpv6(s);
    if (id == QLatin1String("mac")) return matches(s, "[0-9A-Fa-f]{2}([:-])[0-9A-Fa-f]{2}(\\1[0-9A-Fa-f]{2}){4}");
    if (id == QLatin1String("username")) return matches(s, "[A-Za-z0-9][A-Za-z0-9_.-]{2,31}");
    if (id == QLatin1String("strong_password"))
        return s.size() >= 8 && matches(s, "(?s).*\\p{Lu}.*") && matches(s, "(?s).*\\p{Ll}.*") && matches(s, "(?s).*[0-9].*")
            && matches(s, "(?s).*[^\\p{L}\\p{M}0-9\\s].*");
    if (id == QLatin1String("slug")) return matches(s, "[a-z0-9]+(-[a-z0-9]+)*");
    if (id == QLatin1String("identifier")) return matches(s, "[A-Za-z_][A-Za-z0-9_]*");
    if (id == QLatin1String("uuid"))
        return matches(s, "\\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\\}?")
            && (s.startsWith(QLatin1Char('{')) == s.endsWith(QLatin1Char('}')));
    if (id == QLatin1String("hex")) return matches(s, "(0[xX])?[0-9A-Fa-f]+");
    if (id == QLatin1String("hex_color")) return matches(s, "#([0-9A-Fa-f]{3}|[0-9A-Fa-f]{6})");
    if (id == QLatin1String("json")) {
        QJsonParseError error;
        QJsonDocument::fromJson(s.toUtf8(), &error);
        return error.error == QJsonParseError::NoError;
    }

    if (id == QLatin1String("phone_nanp")) {
        // a 10-digit number (a 1 in front is fine), and an extension
        static const QRegularExpression ext(QStringLiteral("\\s*(x|ext\\.?)\\s*[0-9]{1,6}$"), QRegularExpression::CaseInsensitiveOption);
        QString d = digitsOf(QString(s).remove(ext), "+()-. ");
        if (d.size() == 11 && d.startsWith(QLatin1Char('1')))
            d = d.mid(1);
        return matches(d, "[2-9][0-9]{2}[2-9][0-9]{6}");
    }
    if (id == QLatin1String("phone_e164")) {
        if (!s.startsWith(QLatin1Char('+')))
            return false;
        return matches(digitsOf(s.mid(1), "()-. "), "[1-9][0-9]{6,14}");
    }
    if (id == QLatin1String("zip_us")) return matches(s, "[0-9]{5}(-[0-9]{4})?");
    if (id == QLatin1String("postal_ca")) return matches(s, "[ABCEGHJ-NPRSTVXY][0-9][ABCEGHJ-NPRSTV-Z] ?[0-9][ABCEGHJ-NPRSTV-Z][0-9]", true);
    if (id == QLatin1String("postcode_uk"))
        return matches(s, "GIR ?0AA|[A-PR-UWYZ]([0-9]{1,2}|[A-HK-Y][0-9]{1,2}|[0-9][A-HJKPSTUW]|[A-HK-Y][0-9][ABEHMNPRVWXY]) ?[0-9][ABD-HJLNP-UW-Z]{2}", true);
    if (id == QLatin1String("postcode_5")) return matches(s, "[0-9]{5}");
    if (id == QLatin1String("postcode_4")) return matches(s, "[0-9]{4}");
    if (id == QLatin1String("country_code"))
        return matches(s, "[A-Z]{2}") && QLocale::codeToTerritory(s) != QLocale::AnyTerritory;
    if (id == QLatin1String("language_code"))
        return matches(s, "[a-z]{2,3}") && QLocale::codeToLanguage(s, QLocale::ISO639) != QLocale::AnyLanguage;
    if (id == QLatin1String("currency_code")) return matches(s, "[A-Z]{3}") && isCurrency(s);

    if (id == QLatin1String("credit_card")) {
        const QString d = digitsOf(s, " -");
        return d.size() >= 12 && d.size() <= 19 && luhn(d);
    }
    if (id == QLatin1String("iban")) return isIban(s);
    if (id == QLatin1String("bic")) return matches(s, "[A-Z]{4}[A-Z]{2}[A-Z0-9]{2}([A-Z0-9]{3})?");
    if (id == QLatin1String("routing_us")) return isRouting(s);
    if (id == QLatin1String("ssn_us")) return matches(s, "(?!000|666|9[0-9]{2})[0-9]{3}-?(?!00)[0-9]{2}-?(?!0000)[0-9]{4}");
    if (id == QLatin1String("ein_us")) return matches(s, "[0-9]{2}-?[0-9]{7}");
    if (id == QLatin1String("isbn")) return isIsbn(s);
    if (id == QLatin1String("barcode")) {
        const QString d = digitsOf(s, " -");
        return (d.size() == 8 || d.size() == 12 || d.size() == 13 || d.size() == 14) && gtin(d);
    }
    if (id == QLatin1String("vin")) return matches(s, "[A-HJ-NPR-Z0-9]{17}", true);
    return true;
}

bool numberPasses(const QString &id, double n, const QVariant &arg)
{
    const double a = arg.toDouble();
    if (id == QLatin1String("positive")) return n > 0;
    if (id == QLatin1String("not_negative")) return n >= 0;
    if (id == QLatin1String("negative")) return n < 0;
    if (id == QLatin1String("not_zero")) return n != 0;
    if (id == QLatin1String("greater_than")) return n > a;
    if (id == QLatin1String("less_than")) return n < a;
    if (id == QLatin1String("not_equal")) return n != a;
    if (id == QLatin1String("multiple_of")) return std::fabs(std::remainder(n, a)) <= 1e-9 * std::max({ 1.0, std::fabs(n), std::fabs(a) });
    if (id == QLatin1String("whole")) return n == std::floor(n);
    if (id == QLatin1String("max_decimals")) {
        const double scaled = n * std::pow(10.0, arg.toInt());
        return std::fabs(scaled - std::round(scaled)) <= 1e-9 * std::max(1.0, std::fabs(scaled));
    }
    if (id == QLatin1String("even")) return std::fmod(n, 2.0) == 0;
    if (id == QLatin1String("odd")) return std::fabs(std::fmod(n, 2.0)) == 1;
    if (id == QLatin1String("percentage")) return n >= 0 && n <= 100;
    if (id == QLatin1String("fraction")) return n >= 0 && n <= 1;
    if (id == QLatin1String("latitude")) return n >= -90 && n <= 90;
    if (id == QLatin1String("longitude")) return n >= -180 && n <= 180;
    if (id == QLatin1String("year")) return n >= 1000 && n <= 9999;
    if (id == QLatin1String("month")) return n >= 1 && n <= 12;
    if (id == QLatin1String("day_of_month")) return n >= 1 && n <= 31;
    if (id == QLatin1String("port")) return n >= 1 && n <= 65535;
    return true;
}

// dates and times as they're stored: yyyy-MM-dd, HH:mm:ss, yyyy-MM-ddTHH:mm:ss
bool whenPasses(const QString &id, const QString &type, const QString &stored, const QVariant &arg)
{
    const bool hasDate = type != QLatin1String("time");
    const QDateTime dt = type == QLatin1String("datetime") ? QDateTime::fromString(stored, Qt::ISODate)
                       : type == QLatin1String("date") ? QDate::fromString(stored, QStringLiteral("yyyy-MM-dd")).startOfDay()
                       : QDateTime();
    const QDate d = dt.date();
    const QTime t = type == QLatin1String("time") ? QTime::fromString(stored, QStringLiteral("HH:mm:ss")) : dt.time();
    const bool dayOnly = type == QLatin1String("date");
    const QDate today = QDate::currentDate();
    const QDateTime now = QDateTime::currentDateTime();

    if (hasDate) {
        if (id == QLatin1String("past")) return dayOnly ? d < today : dt < now;
        if (id == QLatin1String("not_future")) return dayOnly ? d <= today : dt <= now;
        if (id == QLatin1String("future")) return dayOnly ? d > today : dt > now;
        if (id == QLatin1String("not_past")) return dayOnly ? d >= today : dt >= now;
        if (id == QLatin1String("today")) return d == today;
        if (id == QLatin1String("within_past_days"))
            return dayOnly ? d <= today && d >= today.addDays(-arg.toInt()) : dt <= now && dt >= now.addDays(-arg.toInt());
        if (id == QLatin1String("within_next_days"))
            return dayOnly ? d >= today && d <= today.addDays(arg.toInt()) : dt >= now && dt <= now.addDays(arg.toInt());
        if (id == QLatin1String("min_age")) return wholeYearsSince(d) >= arg.toInt();
        if (id == QLatin1String("max_age")) return wholeYearsSince(d) <= arg.toInt();
        if (id == QLatin1String("weekday")) return d.dayOfWeek() <= 5;
        if (id == QLatin1String("weekend")) return d.dayOfWeek() >= 6;
        if (id == QLatin1String("first_of_month")) return d.day() == 1;
        if (id == QLatin1String("last_of_month")) return d.day() == d.daysInMonth();
    }
    if (id == QLatin1String("whole_hour")) return t.minute() == 0 && t.second() == 0;
    if (id == QLatin1String("minute_step")) return t.second() == 0 && (t.hour() * 60 + t.minute()) % arg.toInt() == 0;
    if (id == QLatin1String("working_hours")) return t >= QTime(9, 0) && t <= QTime(17, 0);
    return true;
}

bool isNumeric(const QString &type)
{
    return QString::fromLatin1(NUMBERS).split(QLatin1Char(' ')).contains(type) || type == QLatin1String("lookup");
}

// two stored values: <0, 0, >0
int compare(const QVariant &a, const QVariant &b, bool numeric)
{
    if (numeric) {
        const double x = a.toDouble(), y = b.toDouble();
        return x < y ? -1 : x > y ? 1 : 0;
    }
    return QString::compare(a.toString(), b.toString());
}

} // namespace

const Rule *find(const QString &id)
{
    for (const Rule &rule : kRules) {
        if (id == QLatin1String(rule.id))
            return &rule;
    }
    return nullptr;
}

QString ruleOf(const QVariantMap &field)
{
    const QString v = field.value(QStringLiteral("validator")).toString();
    if (!v.isEmpty())
        return v;
    return field.value(QStringLiteral("rule")).toString().trimmed().isEmpty() ? QString() : QStringLiteral("expression");
}

QVariantList forType(const QString &type)
{
    static const char *const kArgs[] = { "", "number", "count", "text", "list", "field", "expression" };
    QVariantList out;
    for (const Rule &rule : kRules) {
        if (!hasType(rule, type))
            continue;
        QVariantMap r{
            { QStringLiteral("id"), QLatin1String(rule.id) },
            { QStringLiteral("label"), QString::fromUtf8(rule.label) },
            { QStringLiteral("arg"), QLatin1String(kArgs[int(rule.arg)]) },
            { QStringLiteral("argLabel"), QString::fromUtf8(rule.argLabel) },
            { QStringLiteral("argHint"), QString::fromUtf8(rule.argHint) },
            { QStringLiteral("message"), QString::fromUtf8(rule.message) },
        };
        if (rule.arg == Arg::Field)
            r.insert(QStringLiteral("argTypes"), fieldArgTypes(rule, type));
        out << r;
    }
    return out;
}

QString defaultMessage(const QVariantMap &table, const QVariantMap &field)
{
    const QString what = caption(field);
    if (const Rule *rule = find(ruleOf(field)))
        return message(*rule, table, field, what);
    if (!field.value(QStringLiteral("pattern")).toString().isEmpty())
        return QStringLiteral("%1 isn't in the form it has to be.").arg(what);
    return QString();
}

QString summary(const QVariantMap &table, const QVariantMap &field)
{
    const QString id = ruleOf(field);
    const Rule *rule = find(id);
    if (!rule)
        return id.isEmpty() ? QString() : id;
    if (rule->arg == Arg::Expression)
        return QStringLiteral("rule");
    // the message with the field taken out of it: "has to start with "AB""
    QString s = message(*rule, table, field, QString()).trimmed();
    if (s.endsWith(QLatin1Char('.')))
        s.chop(1);
    return s;
}

Check check(const QVariantMap &table, const QVariantMap &field, const QVariant &value, bool blank,
            const std::function<QVariant(const QVariantMap &field)> &valueOf)
{
    Check out;
    const QString id = ruleOf(field);
    const Rule *rule = find(id);
    if (id.isEmpty() || (rule && rule->arg == Arg::Expression))
        return out;
    const QString what = caption(field);
    auto broken = [&](const QString &problem) {
        out.result = Result::Broken;
        out.problem = problem;
        return out;
    };
    if (!rule)
        return broken(QStringLiteral("%1's validation rule isn't one this knows (%2).").arg(what, id));

    // the rule's own value
    const QVariant arg = field.value(QStringLiteral("validatorArg"));
    const QString label = QString::fromUtf8(rule->label);
    const QString unset = QStringLiteral("%1's validation rule (%2) has no %3 set.")
                              .arg(what, label, QString::fromUtf8(rule->argLabel).toLower());
    QVariantMap other;
    switch (rule->arg) {
    case Arg::Number: {
        bool ok = false;
        const double a = arg.toDouble(&ok);
        if (!ok || arg.toString().trimmed().isEmpty() || !std::isfinite(a))
            return broken(unset);
        if (id == QLatin1String("multiple_of") && a == 0)
            return broken(QStringLiteral("%1's validation rule can't be a multiple of 0.").arg(what));
        break;
    }
    case Arg::Count: {
        bool ok = false;
        const double a = arg.toDouble(&ok);
        if (!ok || arg.toString().trimmed().isEmpty() || a < 0 || a != std::floor(a))
            return broken(unset);
        if (id == QLatin1String("minute_step") && a < 1)
            return broken(QStringLiteral("%1's validation rule needs a step of 1 minute or more.").arg(what));
        break;
    }
    case Arg::Text:
        if (arg.toString().isEmpty())
            return broken(unset);
        break;
    case Arg::List:
        if (listArg(arg).isEmpty())
            return broken(unset);
        break;
    case Arg::Field:
        if (arg.toString().isEmpty())
            return broken(unset);
        other = fieldNamed(table, arg.toString());
        if (other.isEmpty())
            return broken(QStringLiteral("%1's validation rule is about a field that isn't in the table (%2).").arg(what, arg.toString()));
        break;
    default:
        break;
    }

    const QString type = field.value(QStringLiteral("type")).toString();
    const QString fail = message(*rule, table, field, what);
    auto failed = [&]() {
        out.result = Result::Fail;
        out.problem = fail;
        return out;
    };

    // another field's value, as it will be stored (invalid if it's empty;
    // a Yes/No is empty when it's No)
    auto otherValue = [&]() {
        QVariant v = valueOf(other);
        if (v.typeId() == QMetaType::Bool && !v.toBool())
            v = QVariant();
        return v;
    };

    // the ones about being empty
    if (id == QLatin1String("required_if"))
        return blank && otherValue().isValid() ? failed() : out;
    if (id == QLatin1String("required_unless"))
        return blank && !otherValue().isValid() ? failed() : out;
    if (id == QLatin1String("only_if"))
        return !blank && !otherValue().isValid() ? failed() : out;
    if (blank && id != QLatin1String("same_as"))
        return out;

    if (rule->arg == Arg::Field) {
        const QVariant theirs = otherValue();
        const bool numeric = isNumeric(type) && isNumeric(other.value(QStringLiteral("type")).toString());
        if (id == QLatin1String("same_as")) {
            if (blank != !theirs.isValid())
                return failed();
            return blank || compare(value, theirs, numeric) == 0 ? out : failed();
        }
        if (!theirs.isValid())
            return out;
        const int c = compare(value, theirs, numeric);
        if (id == QLatin1String("different_from")) return c != 0 ? out : failed();
        if (id == QLatin1String("more_than_field") || id == QLatin1String("after_field")) return c > 0 ? out : failed();
        if (id == QLatin1String("at_least_field") || id == QLatin1String("on_or_after_field")) return c >= 0 ? out : failed();
        if (id == QLatin1String("less_than_field") || id == QLatin1String("before_field")) return c < 0 ? out : failed();
        if (id == QLatin1String("at_most_field") || id == QLatin1String("on_or_before_field")) return c <= 0 ? out : failed();
        return out;
    }

    bool passes = true;
    if (isNumeric(type))
        passes = numberPasses(id, value.toDouble(), arg);
    else if (type == QLatin1String("date") || type == QLatin1String("time") || type == QLatin1String("datetime"))
        passes = whenPasses(id, type, value.toString(), arg);
    else if (!value.toString().isEmpty())
        passes = textPasses(id, value.toString(), arg);
    return passes ? out : failed();
}

} // namespace FieldRules
