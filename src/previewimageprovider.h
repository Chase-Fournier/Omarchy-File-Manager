#pragma once

#include "preview.h"

#include <QQuickImageProvider>

// Hands the already-decoded QImage to QML without a second decode or a temp file. The
// URL carries a counter so a new image is never served from Qt's pixmap cache under the
// same name as the old one.
class PreviewImageProvider : public QQuickImageProvider
{
public:
    explicit PreviewImageProvider(Preview *preview)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_preview(preview)
    {
    }

    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        const QImage image = m_preview->image();
        if (size)
            *size = image.size();
        return image;
    }

private:
    Preview *m_preview = nullptr;
};
