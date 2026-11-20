#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QObject>
#include <QQuickImageProvider>
#include <QRect>
#include <QtQmlIntegration/qqmlintegration.h>

class QJSEngine;
class QQmlEngine;

// The photo being edited: the picture, its undo and redo steps, and the
// edits (rotate, flip, crop, resize, light and color, effects). QML shows
// it as `imageUrl` (image://photo/..., which changes with each edit).
//
// Light and color are previewed on a screen-sized copy while the sliders
// move (preview()), then applied to the whole picture (adjust()).
//
// Saving never happens behind your back: the window asks before Save
// writes over the original. JPEGs keep their camera information (EXIF).
//
// The QML singleton `PhotoEditor`, one per program.
class PhotoEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString path READ path NOTIFY loadedChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString undoText READ undoText NOTIFY changed)
    Q_PROPERTY(QString redoText READ redoText NOTIFY changed)
    Q_PROPERTY(int width READ width NOTIFY changed)
    Q_PROPERTY(int height READ height NOTIFY changed)
    Q_PROPERTY(bool previewing READ previewing NOTIFY changed)
    Q_PROPERTY(QString imageUrl READ imageUrl NOTIFY changed)

public:
    explicit PhotoEditor(QObject *parent = nullptr);
    static PhotoEditor *instance();
    static PhotoEditor *create(QQmlEngine *, QJSEngine *);

    QString path() const { return m_path; }
    bool loaded() const { return !m_image.isNull(); }
    bool modified() const { return m_savedAt != m_undo.size() || m_savedLost; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString undoText() const { return m_undo.isEmpty() ? QString() : m_undo.last().what; }
    QString redoText() const { return m_redo.isEmpty() ? QString() : m_redo.last().what; }
    int width() const { return m_image.width(); }
    int height() const { return m_image.height(); }
    bool previewing() const { return !m_preview.isNull(); }
    QString imageUrl() const;

    // what's shown: the preview while there is one, else the picture
    QImage displayImage() const;

    // "" or what went wrong
    Q_INVOKABLE QString load(const QString &path);
    Q_INVOKABLE void close();

    Q_INVOKABLE void rotate(int degrees);           // 90, -90, 180
    Q_INVOKABLE void flip(bool horizontal);
    Q_INVOKABLE void crop(int x, int y, int w, int h);
    Q_INVOKABLE void resize(int w, int h);
    // -100..100 each
    Q_INVOKABLE void adjust(int brightness, int contrast, int saturation);
    Q_INVOKABLE void preview(int brightness, int contrast, int saturation);
    Q_INVOKABLE void cancelPreview();
    Q_INVOKABLE void grayscale();
    Q_INVOKABLE void sepia();
    Q_INVOKABLE void autoEnhance();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // back to the file as saved (an edit itself: Undo brings the changes back)
    Q_INVOKABLE void revert();

    // writes the picture to `path` (its type from the suffix); "" or what
    // went wrong. The editor's file becomes `path`.
    Q_INVOKABLE QString save(const QString &path);

    // the image operations themselves (also for tests)
    static QImage adjusted(const QImage &src, int brightness, int contrast, int saturation);
    static QImage grayscaled(const QImage &src);
    static QImage sepiaToned(const QImage &src);
    static QImage enhanced(const QImage &src);

signals:
    void loadedChanged();
    void changed();

private:
    struct Step
    {
        QImage image;
        QString what;
    };
    void apply(const QImage &result, const QString &what);
    void trimUndo();
    void bump();

    static PhotoEditor *s_instance;
    QString m_path;
    QImage m_image;
    QImage m_preview;
    QImage m_previewBase; // the picture at screen size, for previews
    QList<Step> m_undo;
    QList<Step> m_redo;
    int m_savedAt = 0;          // m_undo's size when last saved
    bool m_savedLost = false;   // the saved state fell off the undo list
    int m_revision = 0;
    QByteArray m_exif;          // the original's EXIF segment (JPEG)
};

// image://photo/<revision>: what PhotoEditor shows
class EditorImageProvider : public QQuickImageProvider
{
public:
    EditorImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
