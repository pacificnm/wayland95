#pragma once

#include <QtGlobal>

#include <vector>

namespace tl {
struct Bar { qint64 time; double open, high, low, close, volume; };
using Bars = std::vector<Bar>;
}
