#include "preview.h"

#include "formatting.h"
#include "thumbnails.h"

#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QTextStream>

namespace {

// §11: the first 200 lines, which is enough to recognise a file and cheap on a 2 GB log.
constexpr int kMaxLines = 200;
constexpr qint64 kMaxTextBytes = 512 * 1024;

bool looksLikeText(const QByteArray &sample)
{
    if (sample.isEmpty())
        return true;
    // A NUL byte in the first block is the classic, and reliable, binary tell.
    if (sample.contains('\0'))
        return false;

    int printable = 0;
    for (const char c : sample) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 0x20 || c == '\n' || c == '\t' || c == '\r')
            ++printable;
    }
    return printable * 10 >= sample.size() * 9;
}

} // namespace

void PreviewWorker::load(const QString &path, int maxWidth, int maxHeight, quint64 generation)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        emit loaded(generation, Preview::Empty, QString(), QString(), QImage());
        return;
    }

    // A directory has no preview, but a blank pane reads as a bug rather than as an
    // answer. Counting is capped so a directory with a million entries cannot turn
    // moving the cursor into a stall.
    if (info.isDir()) {
        constexpr int kCountCap = 10000;
        int count = 0;
        QDirIterator entries(path, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        while (entries.hasNext() && count <= kCountCap) {
            entries.next();
            ++count;
        }
        const QString detail = count > kCountCap
            ? QStringLiteral("%1+ items").arg(kCountCap)
            : (count == 1 ? QStringLiteral("1 item")
                          : QStringLiteral("%1 items").arg(count));
        emit loaded(generation, Preview::Other, QString(), detail, QImage());
        return;
    }

    const QString size = Formatting::humanSize(info.size());

    // QImageReader decides by content, not extension, and canRead() is cheap.
    QImageReader reader(path);
    if (reader.canRead()) {
        const QSize original = reader.size();
        if (original.isValid() && maxWidth > 0 && maxHeight > 0) {
            // setScaledSize before read(): this is what stops a 40 MP photo from being
            // fully decoded just to draw it 400 px wide (§11).
            QSize target = original;
            target.scale(maxWidth, maxHeight, Qt::KeepAspectRatio);
            if (target.width() < original.width())
                reader.setScaledSize(target);
        }
        reader.setAutoTransform(true);

        const QImage image = reader.read();
        if (!image.isNull()) {
            // Deliberately *not* read back for the preview itself: a 256 px cache entry
            // would make a full-pane preview blurry. The cache is written so the rest of
            // the desktop — and any future thumbnail grid — gets it for free (§11).
            Thumbnails::store(path, Thumbnails::scaleForCache(image, Thumbnails::Large),
                              Thumbnails::Large);
            const QString detail = original.isValid()
                ? QStringLiteral("%1 × %2 · %3").arg(original.width()).arg(original.height())
                      .arg(size)
                : size;
            emit loaded(generation, Preview::Image, QString(), detail, image);
            return;
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit loaded(generation, Preview::Other, QString(),
                    QStringLiteral("%1 · unreadable").arg(size), QImage());
        return;
    }

    const QByteArray sample = file.peek(4096);
    if (!looksLikeText(sample)) {
        const QString type = QMimeDatabase().mimeTypeForFile(info).name();
        emit loaded(generation, Preview::Other, QString(),
                    QStringLiteral("%1 · %2").arg(type, size), QImage());
        return;
    }

    QString text;
    int lines = 0;
    QTextStream stream(&file);
    while (!stream.atEnd() && lines < kMaxLines && text.size() < kMaxTextBytes) {
        text += stream.readLine();
        text += QLatin1Char('\n');
        ++lines;
    }
    const bool truncated = !stream.atEnd();

    emit loaded(generation, Preview::Text, text,
                truncated ? QStringLiteral("%1 · first %2 lines").arg(size).arg(lines)
                          : size,
                QImage());
}

Preview::Preview(QObject *parent)
    : QObject(parent)
{
}

Preview::~Preview()
{
    if (!m_thread)
        return;
    m_thread->quit();
    m_thread->wait();
}

void Preview::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();

    if (m_enabled)
        request();
    else
        clear();
}

void Preview::show(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    // Closed pane, no work: §11 wants previews off by default and free when they are.
    if (!m_enabled)
        return;
    request();
}

void Preview::clear()
{
    ++m_generation;
    m_kind = Empty;
    m_text.clear();
    m_detail.clear();
    m_image = QImage();
    setLoading(false);
    emit contentChanged();
}

void Preview::request()
{
    if (m_path.isEmpty()) {
        clear();
        return;
    }

    // §12: nothing about previewing exists until a preview is asked for.
    if (!m_thread) {
        m_thread = new QThread(this);
        m_worker = new PreviewWorker;
        m_worker->moveToThread(m_thread);
        connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(m_worker, &PreviewWorker::loaded, this, &Preview::onLoaded);
        m_thread->start();
    }

    ++m_generation;
    setLoading(true);
    QMetaObject::invokeMethod(m_worker, "load", Qt::QueuedConnection, Q_ARG(QString, m_path),
                              Q_ARG(int, 1200), Q_ARG(int, 1200),
                              Q_ARG(quint64, m_generation));
}

void Preview::onLoaded(quint64 generation, int kind, const QString &text, const QString &detail,
                       const QImage &image)
{
    // A decode that finished after the selection moved on is dropped, not shown.
    if (generation != m_generation)
        return;

    m_kind = kind;
    m_text = text;
    m_detail = detail;
    m_image = image;
    setLoading(false);
    emit contentChanged();
    if (!m_image.isNull())
        emit imageReady(m_path);
}

void Preview::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}
