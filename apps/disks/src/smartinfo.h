#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

// smartctl's JSON report (smartctl -x --json=c) made into what Disk
// Manager's S.M.A.R.T. window shows:
//
//   ok, error, exitStatus (smartctl's flags), protocol ("ATA", "NVMe",
//   "SCSI"), model, family, serial, firmware, capacity, rpm (0 SSD),
//   formFactor, interfaceSpeed, smartAvailable, smartEnabled,
//   health ("ok", "warning", "failing", "" unknown), problems (plain words,
//   one each), temperature, powerOnHours, powerCycles,
//   attributes: ATA [{ id, name, value, worst, thresh, raw, type, status, bad }],
//               NVMe and SCSI [{ name, raw, bad }]
//   selfTests [{ type, status, hours, ok }], canSelfTest, testRunning,
//   testRemaining (percent still to go), testStatus, shortMinutes, longMinutes,
//   messages (smartctl's own notes)
QVariantMap parseSmartctl(const QByteArray &json);

// what smartctl said, in one string (its "messages"), or ""
QString smartctlMessage(const QByteArray &json);
