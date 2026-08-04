#include "clipboard.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

namespace {

const QString kGnomeFormat = QStringLiteral("x-special/gnome-copied-files");

QList<QUrl> toUrls(const QStringList &paths)
{
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString &path : paths)
        urls.append(QUrl::fromLocalFile(path));
    return urls;
}

QStringList localPathsFrom(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    return paths;
}

} // namespace

namespace Clipboard {

void setPaths(const QStringList &paths, bool cut)
{
    if (paths.isEmpty())
        return;

    auto *data = new QMimeData;
    data->setUrls(toUrls(paths));
    data->setText(paths.join(QLatin1Char('\n')));

    // "copy\nfile:///a\nfile:///b" — the leading verb is the part uri-list cannot express.
    QByteArray gnome = cut ? QByteArrayLiteral("cut") : QByteArrayLiteral("copy");
    for (const QString &path : paths) {
        gnome += '\n';
        gnome += QUrl::fromLocalFile(path).toEncoded();
    }
    data->setData(kGnomeFormat, gnome);

    QGuiApplication::clipboard()->setMimeData(data);
}

QStringList paths(bool *cut)
{
    if (cut)
        *cut = false;

    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data)
        return {};

    // Preferred: it says whether the files were cut.
    if (data->hasFormat(kGnomeFormat)) {
        const QByteArray payload = data->data(kGnomeFormat);
        const QList<QByteArray> lines = payload.split('\n');
        if (!lines.isEmpty()) {
            if (cut)
                *cut = lines.first().trimmed() == QByteArrayLiteral("cut");

            QStringList out;
            for (int i = 1; i < lines.size(); ++i) {
                const QByteArray line = lines.at(i).trimmed();
                if (line.isEmpty())
                    continue;
                const QUrl url = QUrl::fromEncoded(line);
                if (url.isLocalFile())
                    out.append(url.toLocalFile());
            }
            if (!out.isEmpty())
                return out;
        }
    }

    // Anything else that offers files is treated as a copy, which is the safe reading.
    if (data->hasUrls())
        return localPathsFrom(data->urls());

    return {};
}

bool hasPaths()
{
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    return data && (data->hasUrls() || data->hasFormat(kGnomeFormat));
}

void setText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

} // namespace Clipboard
