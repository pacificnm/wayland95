#!/bin/bash
# Tests the Calculator's arithmetic (CalcEngine) through `win95calc --keys`:
# each line is the keys pressed (separated by spaces), what the display
# should say, and what's being checked.
#
#   tests/calc/run.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${BUILD:-$HERE/../../build}
CALC=$BUILD/win95calc
[ -x "$CALC" ] || { echo "Missing $CALC: build first" >&2; exit 2; }
export QT_QPA_PLATFORM=offscreen
pass=0 fail=0
check() { # OPTIONS KEYS WANT WHAT
    local got
    got=$("$CALC" $1 --keys "$2" 2>/dev/null)
    if [ "$got" = "$3" ]; then echo "PASS  $4"; pass=$((pass + 1))
    else echo "FAIL  $4"; echo "      $2 -> '$got', expected '$3'"; fail=$((fail + 1)); fi
}
S=""; X="--scientific"

echo "== Standard"
check "$S" "1 2 + 3 4 =" "46" "adds"
check "$S" "2 + 3 * 4 =" "20" "left to right as typed (Standard): 2 + 3 * 4 = 20"
check "$S" "7 - 1 0 =" "-3" "a negative result"
check "$S" "1 / 4 =" "0.25" "a fraction"
check "$S" "1 / 3 =" "0.3333333333333333" "1/3 to 16 digits"
check "$S" "2 / 3 * 3 =" "2" "(2/3)*3 comes back to 2"
check "$S" "0 . 1 + 0 . 2 =" "0.3" "0.1 + 0.2 shows 0.3"
check "$S" "5 / 0 =" "Cannot divide by zero." "dividing by zero says so"
check "$S" "5 / 0 = 7" "7" "a number after an error starts again"
check "$S" "5 / 0 = + 7" "7" "(an operator after an error is left out)"
check "$S" "2 + 3 = = =" "11" "= again repeats the last operation"
check "$S" "2 + 3 = 1 0 =" "13" "= after a new number: that number, the same operation"
check "$S" "5 + =" "10" "5 + = is 5 + 5"
check "$S" "5 + * 2 =" "10" "an operator after another replaces it"
check "$S" "9 sqrt" "3" "square root"
check "$S" "2 +/- sqrt" "Invalid input for function." "the square root of a negative number"
check "$S" "4 1/x" "0.25" "1/x"
check "$S" "0 1/x" "Cannot divide by zero." "1/0"
check "$S" "2 0 0 + 5 %" "10" "200 + 5% shows 10 (5% of 200)"
check "$S" "2 0 0 + 5 % =" "210" "and = makes it 210"
check "$S" "2 0 0 * 5 % =" "10" "200 * 5% = 10"
check "$S" "1 2 3 Back" "12" "Back takes off the last digit"
check "$S" "1 2 3 Back Back Back" "0" "(down to 0)"
check "$S" "5 + 3 CE 4 =" "9" "CE clears only the number being typed"
check "$S" "5 + 3 Clear" "0" "C clears it all"
check "$S" "5 +/-" "-5" "+/-"
check "$S" "5 +/- +/-" "5" "+/- twice"
check "$S" "5 + 3 +/- =" "2" "+/- on the second number"
check "$S" "0 0 0 7" "7" "leading zeros"
check "$S" ". 5" "0.5" "a point first is 0."
check "$S" "1 . 2 . 3" "1.23" "a second point is left out"
check "$S" "1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8" "1234567890123456" "16 digits at most"
check "$S" "9 9 9 9 9 9 9 9 * 9 9 9 9 9 9 9 9 =" "9999999800000001" "a big product, exact"
check "$S" "9 9 9 9 9 9 9 9 9 * 9 9 9 9 9 9 9 9 9 =" "9.99999998e+17" "bigger: in e notation"
check "$S" "1 / 1 0 0 0 0 0 0 0 0 0 0 =" "1e-10" "very small: in e notation"
check "$S" "5 MS Clear 3 + MR =" "8" "MS and MR"
check "$S" "5 M+ 3 M+ MR" "8" "M+ adds to memory"
check "$S" "5 MS MC MR" "0" "MC clears it"
check "--scientific" "1 0 x^y 5 0 0 0 =" "Overflow." "too big: Overflow"

echo "== Scientific"
check "$X" "2 + 3 * 4 =" "14" "the usual order (Scientific): 2 + 3 * 4 = 14"
check "$X" "( 2 + 3 ) * 4 =" "20" "brackets"
check "$X" "2 * ( 3 + ( 4 - 1 ) ) =" "12" "brackets in brackets"
check "$X" "( 2 + 3" "3" "(an open bracket waits)"
check "$X" "( 2 + 3 ) =" "5" "a closing bracket and ="
check "$X" "2 x^y 1 0 =" "1024" "x^y"
check "$X" "2 x^y 3 x^y 2 =" "512" "x^y goes right to left: 2^3^2 = 2^9"
check "$X" "2 * 3 x^y 2 =" "18" "x^y before *"
check "$X" "2 7 Inv x^y 3 =" "3" "Inv x^y: the y-th root"
check "$X" "8 +/- Inv x^y 3 =" "-2" "the cube root of -8"
check "$X" "5 x^2" "25" "x^2"
check "$X" "1 6 Inv x^2" "4" "Inv x^2: square root"
check "$X" "3 x^3" "27" "x^3"
check "$X" "2 7 Inv x^3" "3" "Inv x^3: cube root"
check "$X" "3 0 sin" "0.5" "sin 30 (degrees)"
check "$X" "1 8 0 sin" "0" "sin 180 is 0 (not 1.2e-16)"
check "$X" "6 0 cos" "0.5" "cos 60"
check "$X" "4 5 tan" "1" "tan 45"
check "$X" "9 0 tan" "Invalid input for function." "tan 90"
check "$X" "0 . 5 Inv sin" "30" "Inv sin: asin, in degrees"
check "$X" "2 Inv sin" "Invalid input for function." "asin 2"
check "--scientific --angle rad" "pi sin" "0" "sin pi (radians)"
check "--scientific --angle grad" "1 0 0 sin" "1" "sin 100 grads"
check "$X" "1 Hyp sin" "1.175201193643801" "Hyp sin: sinh"
check "$X" "1 Hyp Inv tan" "Invalid input for function." "atanh 1"
check "$X" "1 0 0 log" "2" "log"
check "$X" "2 Inv log" "100" "Inv log: 10^x"
check "$X" "1 ln" "0" "ln 1"
check "$X" "1 Inv ln" "2.718281828459045" "Inv ln: e"
check "$X" "0 ln" "Invalid input for function." "ln 0"
check "$X" "5 n!" "120" "n!"
check "$X" "0 n!" "1" "0! = 1"
check "$X" "1 7 5 5 n!" "Overflow." "1755! is too big"
check "$X" "0 . 5 n!" "0.886226925452758" "n! of a fraction: the gamma function"
check "$X" "pi" "3.141592653589793" "pi"
check "$X" "Inv pi" "6.283185307179586" "Inv pi: 2 pi"
check "$X" "7 . 9 Int" "7" "Int"
check "$X" "7 . 2 5 Inv Int" "0.25" "Inv Int: the fraction"
check "$X" "1 7 Mod 5 =" "2" "Mod"
check "$X" "1 + 1 7 Mod 5 =" "3" "Mod before +"

echo "== Hex, Oct, Bin"
check "--scientific --base 16" "F F" "FF" "hex digits"
check "--scientific --base 16" "F F + 1 =" "100" "hex arithmetic"
check "--scientific --base 2" "1 0 1 + 1 =" "110" "binary"
check "--scientific --base 2" "1 2 1" "11" "(2 isn't a binary digit)"
check "--scientific --base 8" "7 + 1 =" "10" "octal"
check "$X" "2 5 5 base=16" "FF" "Dec to Hex converts what's shown"
check "$X" "2 5 5 . 9 base=16" "FF" "(a fraction is cut off)"
check "--scientific --base 16" "F F base=10" "255" "and back"
check "--scientific --base 16" "1 +/-" "FFFFFFFFFFFFFFFF" "negative: 64-bit two's complement"
check "--scientific --base 16" "F 0 And 3 C =" "30" "And"
check "--scientific --base 16" "F 0 Or 0 F =" "FF" "Or"
check "--scientific --base 16" "F F Xor 0 F =" "F0" "Xor"
check "--scientific --base 2" "1 Lsh 1 0 0 =" "10000" "Lsh"
check "--scientific --base 2" "1 0 0 0 0 Inv Lsh 1 0 0 =" "1" "Inv Lsh: right shift"
check "--scientific --base 16" "0 Not" "FFFFFFFFFFFFFFFF" "Not"
check "--scientific --base 10" "7 / 2 =" "3.5" "Dec divides with fractions"
check "--scientific --base 16" "7 / 2 =" "3" "Hex divides whole numbers"
check "--scientific --base 16" "1 Or 2 And 3 =" "3" "And before Or"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
