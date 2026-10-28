/*
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */
#ifndef HARDWAREPROFILE_H
#define HARDWAREPROFILE_H

#include <QString>
#include <QVector>

class IAudioService;

enum class HardwareProfileId
{
    Vt1705Analog = 1,
    Ep2220 = 2,
    Hdmi = 3
};

struct HardwareProfileDefinition
{
    HardwareProfileId id;
    QString name;
    QString description;
};

struct HardwareProfileChange
{
    QString key;
    QString value;
};

struct HardwareProfilePlan
{
    QVector<HardwareProfileChange> changes;
    QString error;
    bool writesConfig = false;
};

struct HardwareProfileResult
{
    int keysWritten = 0;
    bool saved = false;
    QString error;
    QString status;
};

class HardwareProfile
{
public:
    static QVector<HardwareProfileDefinition> definitions();
    static HardwareProfilePlan plan(HardwareProfileId id);
    static HardwareProfileResult apply(HardwareProfileId id);
    static QString latencyText(const IAudioService *service);
};

#endif // HARDWAREPROFILE_H
