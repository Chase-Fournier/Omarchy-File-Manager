#pragma once

#include <QImage>
#include <QObject>
#include <QThread>

// §11's preview pane: off by default, Ctrl+P, the right 40% of the window.
//
// Images go through QImageReader with setScaledSize so a 40 MP photo never fully decodes.
// Text shows its first 200 lines, monospace, with no syntax highlighting — dead simple is
// the point. Everything else says what it is and how big, which is more useful than a
// blank pane.
class Preview : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString path READ path NOTIFY contentChanged)
    Q_PROPERTY(int kind READ kind NOTIFY contentChanged)
    Q_PROPERTY(QString text READ text NOTIFY contentChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY contentChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    enum Kind { Empty, Image, Text, Other };
    Q_ENUM(Kind)

    explicit Preview(QObject *parent = nullptr);
    ~Preview() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QString path() const { return m_path; }
    int kind() const { return m_kind; }
    QString text() const { return m_text; }
    QString detail() const { return m_detail; }
    bool loading() const { return m_loading; }

    // Called as the selection moves. Cheap when the pane is closed: it does nothing.
    Q_INVOKABLE void show(const QString &path);
    Q_INVOKABLE void clear();

    // The decoded image, handed to QML through an image provider rather than a property.
    QImage image() const { return m_image; }

signals:
    void enabledChanged();
    void contentChanged();
    void loadingChanged();
    // Carries a generation so a stale decode cannot replace a newer one.
    void imageReady(const QString &path);

private slots:
    void onLoaded(quint64 generation, int kind, const QString &text, const QString &detail,
                  const QImage &image);

private:
    void request();
    void setLoading(bool loading);

    bool m_enabled = false;
    QString m_path;
    int m_kind = Empty;
    QString m_text;
    QString m_detail;
    QImage m_image;
    bool m_loading = false;

    quint64 m_generation = 0;
    QThread *m_thread = nullptr;
    class PreviewWorker *m_worker = nullptr;
};

// Runs on its own thread: reading and decoding are exactly the filesystem work §3 keeps
// off the GUI thread, and a preview must never be the reason scrolling stutters.
class PreviewWorker : public QObject
{
    Q_OBJECT

public slots:
    void load(const QString &path, int maxWidth, int maxHeight, quint64 generation);

signals:
    void loaded(quint64 generation, int kind, const QString &text, const QString &detail,
                const QImage &image);
};
