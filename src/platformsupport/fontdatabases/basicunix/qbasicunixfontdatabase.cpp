/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qbasicunixfontdatabase_p.h"

#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/QPlatformScreen>

#include <QtCore/QFile>
#include <QtCore/QLibraryInfo>
#include <QtCore/QDir>
#include <QtCore/QUuid>

#undef QT_NO_FREETYPE
#include <QtGui/private/qfontengine_ft_p.h>
#include <QtGui/private/qfontengine_p.h>

#include <ft2build.h>
#include FT_TRUETYPE_TABLES_H

#define SimplifiedChineseCsbBit 18
#define TraditionalChineseCsbBit 20
#define JapaneseCsbBit 17
#define KoreanCsbBit 21

static int requiredUnicodeBits[QFontDatabase::WritingSystemsCount][2] = {
        // Any,
    { 127, 127 },
        // Latin,
    { 0, 127 },
        // Greek,
    { 7, 127 },
        // Cyrillic,
    { 9, 127 },
        // Armenian,
    { 10, 127 },
        // Hebrew,
    { 11, 127 },
        // Arabic,
    { 13, 127 },
        // Syriac,
    { 71, 127 },
    //Thaana,
    { 72, 127 },
    //Devanagari,
    { 15, 127 },
    //Bengali,
    { 16, 127 },
    //Gurmukhi,
    { 17, 127 },
    //Gujarati,
    { 18, 127 },
    //Oriya,
    { 19, 127 },
    //Tamil,
    { 20, 127 },
    //Telugu,
    { 21, 127 },
    //Kannada,
    { 22, 127 },
    //Malayalam,
    { 23, 127 },
    //Sinhala,
    { 73, 127 },
    //Thai,
    { 24, 127 },
    //Lao,
    { 25, 127 },
    //Tibetan,
    { 70, 127 },
    //Myanmar,
    { 74, 127 },
        // Georgian,
    { 26, 127 },
        // Khmer,
    { 80, 127 },
        // SimplifiedChinese,
    { 126, 127 },
        // TraditionalChinese,
    { 126, 127 },
        // Japanese,
    { 126, 127 },
        // Korean,
    { 56, 127 },
        // Vietnamese,
    { 0, 127 }, // same as latin1
        // Other,
    { 126, 127 },
        // Ogham,
    { 78, 127 },
        // Runic,
    { 79, 127 },
        // Nko,
    { 14, 127 },
};

static QSupportedWritingSystems determineWritingSystemsFromTrueTypeBits(quint32 unicodeRange[4], quint32 codePageRange[2])
{
    QSupportedWritingSystems writingSystems;
    bool hasScript = false;

    int i;
    for(i = 0; i < QFontDatabase::WritingSystemsCount; i++) {
        int bit = requiredUnicodeBits[i][0];
        int index = bit/32;
        int flag =  1 << (bit&31);
        if (bit != 126 && unicodeRange[index] & flag) {
            bit = requiredUnicodeBits[i][1];
            index = bit/32;

            flag =  1 << (bit&31);
            if (bit == 127 || unicodeRange[index] & flag) {
                writingSystems.setSupported(QFontDatabase::WritingSystem(i));
                hasScript = true;
                // qDebug("font %s: index=%d, flag=%8x supports script %d", familyName.latin1(), index, flag, i);
            }
        }
    }
    if(codePageRange[0] & (1 << SimplifiedChineseCsbBit)) {
        writingSystems.setSupported(QFontDatabase::SimplifiedChinese);
        hasScript = true;
        //qDebug("font %s supports Simplified Chinese", familyName.latin1());
    }
    if(codePageRange[0] & (1 << TraditionalChineseCsbBit)) {
        writingSystems.setSupported(QFontDatabase::TraditionalChinese);
        hasScript = true;
        //qDebug("font %s supports Traditional Chinese", familyName.latin1());
    }
    if(codePageRange[0] & (1 << JapaneseCsbBit)) {
        writingSystems.setSupported(QFontDatabase::Japanese);
        hasScript = true;
        //qDebug("font %s supports Japanese", familyName.latin1());
    }
    if(codePageRange[0] & (1 << KoreanCsbBit)) {
        writingSystems.setSupported(QFontDatabase::Korean);
        hasScript = true;
        //qDebug("font %s supports Korean", familyName.latin1());
    }
    if (!hasScript)
        writingSystems.setSupported(QFontDatabase::Symbol);

    return writingSystems;
}

static inline bool scriptRequiresOpenType(int script)
{
    return ((script >= QUnicodeTables::Syriac && script <= QUnicodeTables::Sinhala)
            || script == QUnicodeTables::Khmer || script == QUnicodeTables::Nko);
}

void QBasicUnixFontDatabase::populateFontDatabase()
{
    QPlatformFontDatabase::populateFontDatabase();
    QString fontpath = fontDir();

    if(!QFile::exists(fontpath)) {
        qFatal("QFontDatabase: Cannot find font directory %s - is Qt installed correctly?",
               qPrintable(fontpath));
    }

    QDir dir(fontpath);
    dir.setNameFilters(QStringList() << QLatin1String("*.ttf")
                       << QLatin1String("*.ttc") << QLatin1String("*.pfa")
                       << QLatin1String("*.pfb"));
    dir.refresh();
    for (int i = 0; i < int(dir.count()); ++i) {
        const QByteArray file = QFile::encodeName(dir.absoluteFilePath(dir[i]));
//        qDebug() << "looking at" << file;
        addTTFile(QByteArray(), file);
    }
}

QFontEngine *QBasicUnixFontDatabase::fontEngine(const QFontDef &fontDef, QUnicodeTables::Script script, void *usrPtr)
{
    QFontEngineFT *engine;
    FontFile *fontfile = static_cast<FontFile *> (usrPtr);
    QFontEngine::FaceId fid;
    fid.filename = fontfile->fileName.toLocal8Bit();
    fid.index = fontfile->indexValue;
    engine = new QFontEngineFT(fontDef);

    bool antialias = !(fontDef.styleStrategy & QFont::NoAntialias);
    QFontEngineFT::GlyphFormat format = antialias? QFontEngineFT::Format_A8 : QFontEngineFT::Format_Mono;
    if (!engine->init(fid,antialias,format)) {
        delete engine;
        engine = 0;
        return engine;
    }
    if (engine->invalid()) {
        delete engine;
        engine = 0;
    } else if (scriptRequiresOpenType(script)) {
        HB_Face hbFace = engine->harfbuzzFace();
        if (!hbFace || !hbFace->supported_scripts[script]) {
            delete engine;
            engine = 0;
        }
    }

    return engine;
}

namespace {

    class QFontEngineFTRawData: public QFontEngineFT
    {
    public:
        QFontEngineFTRawData(const QFontDef &fontDef) : QFontEngineFT(fontDef)
        {
        }

        void updateFamilyNameAndStyle()
        {
            fontDef.family = QString::fromAscii(freetype->face->family_name);

            if (freetype->face->style_flags & FT_STYLE_FLAG_ITALIC)
                fontDef.style = QFont::StyleItalic;

            if (freetype->face->style_flags & FT_STYLE_FLAG_BOLD)
                fontDef.weight = QFont::Bold;
        }

        bool initFromData(const QByteArray &fontData)
        {
            FaceId faceId;
            faceId.filename = "";
            faceId.index = 0;
            faceId.uuid = QUuid::createUuid().toByteArray();

            return init(faceId, true, Format_None, fontData);
        }
    };

}

QFontEngine *QBasicUnixFontDatabase::fontEngine(const QByteArray &fontData, qreal pixelSize,
                                                QFont::HintingPreference hintingPreference)
{
    QFontDef fontDef;
    fontDef.pixelSize = pixelSize;

    QFontEngineFTRawData *fe = new QFontEngineFTRawData(fontDef);
    if (!fe->initFromData(fontData)) {
        delete fe;
        return 0;
    }

    fe->updateFamilyNameAndStyle();

    switch (hintingPreference) {
    case QFont::PreferNoHinting:
        fe->setDefaultHintStyle(QFontEngineFT::HintNone);
        break;
    case QFont::PreferFullHinting:
        fe->setDefaultHintStyle(QFontEngineFT::HintFull);
        break;
    case QFont::PreferVerticalHinting:
        fe->setDefaultHintStyle(QFontEngineFT::HintLight);
        break;
    default:
        // Leave it as it is
        break;
    }

    return fe;
}

QStringList QBasicUnixFontDatabase::fallbacksForFamily(const QString family, const QFont::Style &style, const QFont::StyleHint &styleHint, const QUnicodeTables::Script &script) const
{
    Q_UNUSED(family);
    Q_UNUSED(style);
    Q_UNUSED(script);
    Q_UNUSED(styleHint);
    return QStringList();
}

QStringList QBasicUnixFontDatabase::addApplicationFont(const QByteArray &fontData, const QString &fileName)
{
    return addTTFile(fontData,fileName.toLocal8Bit());
}

void QBasicUnixFontDatabase::releaseHandle(void *handle)
{
    FontFile *file = static_cast<FontFile *>(handle);
    delete file;
}

QStringList QBasicUnixFontDatabase::addTTFile(const QByteArray &fontData, const QByteArray &file)
{
    extern FT_Library qt_getFreetype();
    FT_Library library = qt_getFreetype();

    int index = 0;
    int numFaces = 0;
    QStringList families;
    do {
        FT_Face face;
        FT_Error error;
        if (!fontData.isEmpty()) {
            error = FT_New_Memory_Face(library, (const FT_Byte *)fontData.constData(), fontData.size(), index, &face);
        } else {
            error = FT_New_Face(library, file.constData(), index, &face);
        }
        if (error != FT_Err_Ok) {
            qDebug() << "FT_New_Face failed with index" << index << ":" << hex << error;
            break;
        }
        numFaces = face->num_faces;

        QFont::Weight weight = QFont::Normal;

        QFont::Style style = QFont::StyleNormal;
        if (face->style_flags & FT_STYLE_FLAG_ITALIC)
            style = QFont::StyleItalic;

        if (face->style_flags & FT_STYLE_FLAG_BOLD)
            weight = QFont::Bold;

        bool fixedPitch = (face->face_flags & FT_FACE_FLAG_FIXED_WIDTH);

        QSupportedWritingSystems writingSystems;
        // detect symbol fonts
        for (int i = 0; i < face->num_charmaps; ++i) {
            FT_CharMap cm = face->charmaps[i];
            if (cm->encoding == ft_encoding_adobe_custom
                    || cm->encoding == ft_encoding_symbol) {
                writingSystems.setSupported(QFontDatabase::Symbol);
                break;
            }
        }

        TT_OS2 *os2 = (TT_OS2 *)FT_Get_Sfnt_Table(face, ft_sfnt_os2);
        if (os2) {
            quint32 unicodeRange[4] = {
                quint32(os2->ulUnicodeRange1),
                quint32(os2->ulUnicodeRange2),
                quint32(os2->ulUnicodeRange3),
                quint32(os2->ulUnicodeRange4)
            };
            quint32 codePageRange[2] = {
                quint32(os2->ulCodePageRange1),
                quint32(os2->ulCodePageRange2)
            };

            writingSystems = determineWritingSystemsFromTrueTypeBits(unicodeRange, codePageRange);

            if (os2->usWeightClass == 0)
                ;
            else if (os2->usWeightClass < 350)
                weight = QFont::Light;
            else if (os2->usWeightClass < 450)
                weight = QFont::Normal;
            else if (os2->usWeightClass < 650)
                weight = QFont::DemiBold;
            else if (os2->usWeightClass < 750)
                weight = QFont::Bold;
            else if (os2->usWeightClass < 1000)
                weight = QFont::Black;

            if (os2->panose[2] >= 2) {
                int w = os2->panose[2];
                if (w <= 3)
                    weight = QFont::Light;
                else if (w <= 5)
                    weight = QFont::Normal;
                else if (w <= 7)
                    weight = QFont::DemiBold;
                else if (w <= 8)
                    weight = QFont::Bold;
                else if (w <= 10)
                    weight = QFont::Black;
            }
        }

        QString family = QString::fromAscii(face->family_name);
        FontFile *fontFile = new FontFile;
        fontFile->fileName = QString::fromAscii(file);
        fontFile->indexValue = index;

        QFont::Stretch stretch = QFont::Unstretched;

        registerFont(family,QString(),weight,style,stretch,true,true,0,fixedPitch,writingSystems,fontFile);

        families.append(family);

        FT_Done_Face(face);
        ++index;
    } while (index < numFaces);
    return families;
}
