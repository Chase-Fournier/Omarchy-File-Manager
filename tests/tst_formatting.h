#pragma once

#include <QObject>

class TestFormatting : public QObject
{
    Q_OBJECT

private slots:
    void humanSize_data();
    void humanSize();
    void relativeTime_data();
    void relativeTime();
};
