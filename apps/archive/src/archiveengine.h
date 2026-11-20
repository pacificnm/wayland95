#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

// The work behind Archive Manager, run on a worker thread: reading an
// archive's list, extracting, testing, and writing archives (new ones,
// adding to and deleting from existing ones). libarchive does it where it
// can; the few things it can't are handed to a program, if there is one:
//
//   7-Zip (7z, package 7zip)    7z archives with a password, and making them
//   unar (package unar)         RAR archives libarchive can't read (with a
//                               password, or newer compression)
//
// Archives are never changed in place: adding or deleting writes a new copy
// beside the old one and puts it in its place only once it's complete.

struct ArchiveItem
{
    QString path;       // inside the archive: "docs/readme.txt" (folders without the trailing /)
    bool isDir = false;
    qint64 size = 0;
    qint64 packed = -1; // compressed size, where the format says (zip, single files)
    QDateTime modified;
    bool encrypted = false;
    QString linkTarget; // a symbolic link's target
};

// what a job reports back and asks about while it runs
struct JobContext
{
    std::atomic<bool> cancel{false};
    QString password;
    // progress: bytes done of total (total 0: unknown), and what it's on
    std::function<void(qint64 done, qint64 total, const QString &current)> progress;
    // a file being extracted is already there: Overwrite answer
    std::function<int(const QString &path)> askOverwrite;
};

enum Overwrite { OverwriteYes, OverwriteYesAll, OverwriteNo, OverwriteNoAll, OverwriteCancel };

struct JobResult
{
    bool ok = false;
    bool cancelled = false;
    bool needPassword = false;  // encrypted, and no (or the wrong) password given
    QString error;              // what went wrong, for a message box
    QStringList problems;       // per-file errors (Test), or files skipped
    int files = 0;              // how many files it did
    qint64 bytes = 0;
    QStringList written;        // extract: the paths written (top-level first)
};

struct ListResult : JobResult
{
    QString format;          // "zip", "7z", "tar.gz", "rar", "gz", ...
    QString formatName;      // "ZIP", "7-Zip", "Gzip-compressed tar", ...
    bool encrypted = false;  // any entry needs a password
    QString tool;            // "" (libarchive) or the program reading it ("7z", "unar")
    QVector<ArchiveItem> items;
};

struct WriteOptions
{
    QString format;   // as ListResult::format; only the writable ones (see canWrite)
    int level = 6;    // 0 store ... 9 maximum
    QString password; // zip (AES-256) and 7z (needs the 7z program)
};

namespace ArchiveEngine {

// formats by file name: "zip", "tar.gz", ... ("" if it doesn't look like one)
QString formatForName(const QString &fileName);
// "ZIP archive", "Tar archive (gzip)", ...
QString formatLabel(const QString &format);
// the name without its archive suffix: "photos.tar.gz" -> "photos"
QString baseName(const QString &fileName);
// can Archive Manager write (create, add to, delete from) this format
bool canWrite(const QString &format);
// single-file formats: hold exactly one file (gz, bz2, xz, zst)
bool isSingleFile(const QString &format);
// "" if all's well, else what has to be installed for this, e.g.
// "7-Zip (sudo apt install 7zip)"
QString toolNeeded(const QString &what);

// the programs, found on the PATH ("" if not installed)
QString sevenZip();
QString unar();
QString lsar();

ListResult list(const QString &archive, const QString &password);

// extract `only` (archive paths; a folder takes what's in it; empty: all)
// into dest; useFolders false puts every file straight into dest
JobResult extract(const QString &archive, const QString &format, const QString &tool,
                  const QStringList &only, const QString &dest, bool useFolders,
                  JobContext &ctx);
JobResult test(const QString &archive, const QString &tool, JobContext &ctx);

// add files and folders (absolute paths; each goes in at `inside` + its name)
// creating the archive if it isn't there. Same paths already in it are replaced.
JobResult add(const QString &archive, const QStringList &files, const QString &inside,
              const WriteOptions &options, JobContext &ctx);
// delete entries (folders take what's in them)
JobResult remove(const QString &archive, const QStringList &paths,
                 const WriteOptions &options, JobContext &ctx);

} // namespace ArchiveEngine
