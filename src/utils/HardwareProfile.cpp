/*
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */
#include "HardwareProfile.h"

#include "audio/base/IAudioService.h"
#include "config/AppConfig.h"
#include "config/ConfigIO.h"
#include "config/DspConfig.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QHash>
#include <QLocale>
#include <QMetaEnum>
#include <QSet>
#include <QStringList>

namespace {

const QStringList kAllowedKeys = {
    QStringLiteral("master_postgain"),
    QStringLiteral("master_limthreshold"),
    QStringLiteral("crossfeed_enable"),
    QStringLiteral("convolver_enable"),
    QStringLiteral("compander_time_freq_transforms"),
    QStringLiteral("bass_enable"),
    QStringLiteral("stereowide_enable"),
    QStringLiteral("ddc_enable"),
    QStringLiteral("liveprog_enable"),
    QStringLiteral("liveprog_file")
};

struct ProfileFile
{
    HardwareProfileId id;
    QString name;
    QString resource;
    QString description;
    bool requireLiveprog = false;
};

const ProfileFile kProfiles[] = {
        {
            HardwareProfileId::Ep2220,
            QStringLiteral("Linha verde → EP-2220"),
            QStringLiteral(":/profiles/ep2220.conf"),
            QStringLiteral("Cabo P2 do jack verde da placa, line-out do VT1705, na entrada auxiliar da EP-2220. A outra ponta não vai no P10 de microfone nem no de instrumento. O script corta DC, soma o grave abaixo de 100 Hz em mono e tira o que está abaixo de 70 Hz do woofer de 5,25 pol. Ganho final 0 dB, limiter em -1 dB. Desliga crossfeed, convolver, bass boost, alargador, DDC e o compander modo 2 (passa ao modo 0; a curva salva continua). O equalizador gráfico não muda. 3bandSplitting.eel não é apagado. Copie meta/pipewire/10-vt1705.conf e meta/wireplumber/51-vt1705-line.conf e reinicie o PipeWire você mesmo. Bluetooth, se usar outro dia, não perde atraso com isso."),
            true
        },
        {
            HardwareProfileId::Vt1705Analog,
            QStringLiteral("VT1705 analógico"),
            QStringLiteral(":/profiles/vt1705.conf"),
            QStringLiteral("Codec VIA VT1705 desta H61M-HVS, sem o processador da caixa. O perfil trava o ganho final em 0 dB, põe o limiter em -1 dB e desliga o crossfeed (ele só serve em fone no jack). Mantenha 48 kHz: fora de 44,1–48 kHz o JamesDSP liga o ASRC interno e o codec reamostra de novo. Use o jack verde traseiro, não o frontal. O estalo ao acordar o codec some com power_save=0 no módulo snd-hda-intel; isso é ajuste do kernel, este processo não mexe nisso. Para a latência, copie meta/pipewire/10-vt1705.conf e meta/wireplumber/51-vt1705-line.conf e reinicie o PipeWire você mesmo.")
        },
        {
            HardwareProfileId::Hdmi,
            QStringLiteral("HDMI (Intel ou ATI)"),
            QStringLiteral(":/profiles/hdmi.conf"),
            QStringLiteral("Saída HDMI Intel (CougarPoint) ou ATI R6xx. O conversor é o da TV ou do monitor, então este perfil não grava ganho, limiter nem equalização. Mantenha 48 kHz. Aplicar não altera o audio.conf.")
        }
};

void collectDefinitions(QVector<HardwareProfileDefinition> &list)
{
    for (const auto &profile : kProfiles)
        list.push_back({profile.id, profile.name, profile.description});
}

const ProfileFile *findProfile(HardwareProfileId id)
{
    for (const auto &profile : kProfiles)
    {
        if (profile.id == id)
            return &profile;
    }
    return nullptr;
}

bool allowedKey(const QString &key)
{
    return kAllowedKeys.contains(key);
}

QString loadProfileText(const QString &resource, QString *error)
{
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly))
    {
        *error = QStringLiteral("Não foi possível ler o perfil %1.").arg(resource);
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

HardwareProfilePlan readPlan(HardwareProfileId id)
{
    HardwareProfilePlan plan;
    const ProfileFile *profile = findProfile(id);
    if (profile == nullptr)
    {
        plan.error = QStringLiteral("Perfil desconhecido.");
        return plan;
    }
    QString error;
    const QString text = loadProfileText(profile->resource, &error);
    if (!error.isEmpty())
    {
        plan.error = error;
        return plan;
    }

    const QVariantMap map = ConfigIO::readString(text);
    if (profile->requireLiveprog && !map.contains(QStringLiteral("liveprog_file")))
    {
        plan.error = QStringLiteral("O perfil da EP-2220 não contém liveprog_file.");
        return plan;
    }

    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        if (!allowedKey(it.key()))
        {
            plan.error = QStringLiteral("O perfil contém a chave %1, que este aplicador não pode gravar.").arg(it.key());
            plan.changes.clear();
            plan.writesConfig = false;
            return plan;
        }
        plan.changes.push_back({it.key(), it.value().toString()});
    }

    plan.writesConfig = !plan.changes.isEmpty();
    if (id != HardwareProfileId::Hdmi && !plan.writesConfig)
        plan.error = QStringLiteral("O perfil não contém chaves para aplicar.");
    return plan;
}

QString installLiveprog(const QString &fileName, QString *error)
{
    if (fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\')))
    {
        *error = QStringLiteral("liveprog_file precisa ser só o nome do script.");
        return {};
    }

    const QString directory = AppConfig::instance().getLiveprogPath();
    QDir().mkpath(directory);
    const QString dest = QDir(directory).filePath(fileName);
    if (QFile::exists(dest))
        return dest;

    const QString resource = QStringLiteral(":/assets/liveprog/") + fileName;
    if (!QFile::exists(resource))
    {
        *error = QStringLiteral("O script %1 não está nos recursos do JamesDSP.").arg(fileName);
        return {};
    }

    QFile source(resource);
    if (!source.copy(dest))
    {
        *error = QStringLiteral("Não foi possível copiar %1 para %2.").arg(fileName, dest);
        return {};
    }

    QFile destFile(dest);
    destFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    return dest;
}

int enumValue(const QString &key, bool *ok)
{
    const QMetaEnum meta = QMetaEnum::fromType<DspConfig::Key>();
    const int value = meta.keyToValue(key.toUtf8().constData());
    *ok = value >= 0;
    return value;
}

} // namespace

QVector<HardwareProfileDefinition> HardwareProfile::definitions()
{
    QVector<HardwareProfileDefinition> list;
    collectDefinitions(list);
    return list;
}

HardwareProfilePlan HardwareProfile::plan(HardwareProfileId id)
{
    return readPlan(id);
}

HardwareProfileResult HardwareProfile::apply(HardwareProfileId id)
{
    HardwareProfileResult result;
    const HardwareProfilePlan planned = readPlan(id);
    if (!planned.error.isEmpty())
    {
        result.error = planned.error;
        result.status = planned.error;
        return result;
    }
    if (!planned.writesConfig)
    {
        result.status = QStringLiteral("Nenhuma chave do audio.conf foi gravada. O conversor deste caminho é o da TV ou do monitor.");
        return result;
    }

    QVector<HardwareProfileChange> resolved = planned.changes;
    for (auto &change : resolved)
    {
        if (change.key != QLatin1String("liveprog_file"))
            continue;
        QString error;
        const QString path = installLiveprog(change.value, &error);
        if (!error.isEmpty())
        {
            result.error = error;
            result.status = error;
            return result;
        }
        change.value = path;
    }

    for (const auto &change : resolved)
    {
        bool ok = false;
        enumValue(change.key, &ok);
        if (!ok)
        {
            result.error = QStringLiteral("Chave desconhecida: %1").arg(change.key);
            result.status = result.error;
            return result;
        }
    }

    QMetaEnum meta = QMetaEnum::fromType<DspConfig::Key>();
    QHash<int, QVariant> snapshot;
    for (int index = 0; index < meta.keyCount(); ++index)
    {
        const auto key = static_cast<DspConfig::Key>(meta.value(index));
        snapshot.insert(meta.value(index), DspConfig::instance().get<QVariant>(key));
    }

    const QString graphicEq = DspConfig::instance().get<QString>(DspConfig::graphiceq_param);
    QSet<int> owned;

    for (const auto &change : resolved)
    {
        bool ok = false;
        const int value = enumValue(change.key, &ok);
        if (!ok)
            continue;

        QVariant stored;
        if (change.value == QLatin1String("true"))
            stored = true;
        else if (change.value == QLatin1String("false"))
            stored = false;
        else
            stored = change.value;

        DspConfig::instance().set(static_cast<DspConfig::Key>(value), stored);
        owned.insert(value);
    }

    bool leaked = DspConfig::instance().get<QString>(DspConfig::graphiceq_param) != graphicEq;
    if (!leaked)
    {
        for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it)
        {
            if (owned.contains(it.key()))
                continue;
            const auto key = static_cast<DspConfig::Key>(it.key());
            if (DspConfig::instance().get<QVariant>(key) != it.value())
            {
                leaked = true;
                break;
            }
        }
    }

    if (leaked)
    {
        for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it)
            DspConfig::instance().set(static_cast<DspConfig::Key>(it.key()), it.value());
        result.error = QStringLiteral("O perfil tentou alterar uma chave que não é dele. Nada foi gravado.");
        result.status = result.error;
        Log::error(result.error);
        return result;
    }

    DspConfig::instance().commit();
    DspConfig::instance().save();
    emit DspConfig::instance().configBuffered();

    result.keysWritten = owned.size();
    result.saved = true;
    result.status = QStringLiteral("Perfil aplicado. %1 chaves gravadas. O equalizador gráfico não foi alterado.").arg(result.keysWritten);
    return result;
}

QString HardwareProfile::latencyText(const IAudioService *service)
{
    QStringList lines;
    const PlaybackClock clock = service ? service->playbackClock() : PlaybackClock{};
    if (clock.rateHz > 0 && clock.quantum > 0)
    {
        const double ms = (1000.0 * static_cast<double>(clock.quantum)) / static_cast<double>(clock.rateHz);
        lines << QStringLiteral("Buffer do PipeWire: %1 amostras / %2 Hz = %3 ms.")
                     .arg(clock.quantum)
                     .arg(clock.rateHz)
                     .arg(QLocale().toString(ms, 'f', 1));
    }
    else
    {
        lines << QStringLiteral("Buffer do PipeWire: quantum ainda não lido.");
    }

    if (clock.nodeLatencySeconds > 0.f)
    {
        lines << QStringLiteral("Latência do nó de saída: %1 ms.")
                     .arg(QLocale().toString(static_cast<double>(clock.nodeLatencySeconds) * 1000.0, 'f', 1));
    }

    if (DspConfig::instance().get<bool>(DspConfig::compander_enable))
    {
        const int mode = DspConfig::instance().get<int>(DspConfig::compander_time_freq_transforms);
        int window = 0;
        int hop = 0;
        if (mode == 0)
        {
            window = 2048;
            hop = 256;
        }
        else if (mode == 1 || mode == 2)
        {
            window = 4096;
            hop = 512;
        }
        else if (mode == 3)
        {
            window = 32;
            hop = 32;
        }
        if (window > 0 && hop > 0)
        {
            const int effectRate = clock.rateHz > 0 ? clock.rateHz : 48000;
            const double windowMs = (1000.0 * static_cast<double>(window)) / static_cast<double>(effectRate);
            const double hopMs = (1000.0 * static_cast<double>(hop)) / static_cast<double>(effectRate);
            lines << QStringLiteral("Compander modo %1: janela %2 ms, salto %3 ms.")
                         .arg(mode)
                         .arg(QLocale().toString(windowMs, 'f', 1))
                         .arg(QLocale().toString(hopMs, 'f', 1));
        }
    }
    if (DspConfig::instance().get<bool>(DspConfig::convolver_enable))
        lines << QStringLiteral("O convolver soma o comprimento da resposta ao atraso.");
    lines << QStringLiteral("No cabo P2, esse atraso é o que chega na caixa.");

    return lines.join(QLatin1Char('\n'));
}
