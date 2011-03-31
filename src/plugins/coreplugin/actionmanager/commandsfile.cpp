/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2011 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** No Commercial Usage
**
** This file contains pre-release code and may not be distributed.
** You may use this file in accordance with the terms and conditions
** contained in the Technology Preview License Agreement accompanying
** this package.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights.  These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/

#include "commandsfile.h"
#include "shortcutsettings.h"
#include "command_p.h"

#include <coreplugin/uniqueidmanager.h>
#include <coreplugin/coreconstants.h>

#include <utils/qtcassert.h>

#include <QtCore/QFile>
#include <QtCore/QXmlStreamAttributes>
#include <QtCore/QXmlStreamWriter>
#include <QtCore/QXmlStreamReader>
#include <QtCore/QDebug>
#include <QtCore/QDateTime>

namespace Core {
namespace Internal {

struct Context // XML parsing context with strings.
{
    Context();

    const QString mappingElement;
    const QString shortCutElement;
    const QString idAttribute;
    const QString keyElement;
    const QString valueAttribute;
};

Context::Context() :
    mappingElement(QLatin1String("mapping")),
    shortCutElement(QLatin1String("shortcut")),
    idAttribute(QLatin1String("id")),
    keyElement(QLatin1String("key")),
    valueAttribute(QLatin1String("value"))
{
}

/*!
    \class CommandsFile
    \brief The CommandsFile class provides a collection of import and export commands.
    \inheaderfile commandsfile.h
*/

/*!
    ...
*/
CommandsFile::CommandsFile(const QString &filename)
    : m_filename(filename)
{

}

/*!
    ...
*/
QMap<QString, QKeySequence> CommandsFile::importCommands() const
{
    QMap<QString, QKeySequence> result;

    QFile file(m_filename);
    if (!file.open(QIODevice::ReadOnly|QIODevice::Text))
        return result;

    Context ctx;
    QXmlStreamReader r(&file);

    QString currentId;

    while (!r.atEnd()) {
        switch (r.readNext()) {
        case QXmlStreamReader::StartElement: {
            const QStringRef name = r.name();
            if (name == ctx.shortCutElement) {
                currentId = r.attributes().value(ctx.idAttribute).toString();
            } else if (name == ctx.keyElement) {
                QTC_ASSERT(!currentId.isEmpty(), return result; )
                const QXmlStreamAttributes attributes = r.attributes();
                if (attributes.hasAttribute(ctx.valueAttribute)) {
                    const QString keyString = attributes.value(ctx.valueAttribute).toString();
                    result.insert(currentId, QKeySequence(keyString));
                } else {
                    result.insert(currentId, QKeySequence());
                }
                currentId.clear();
            } // if key element
        } // case QXmlStreamReader::StartElement
        default:
            break;
        } // switch
    } // while !atEnd
    file.close();
    return result;
}

/*!
    ...
*/

bool CommandsFile::exportCommands(const QList<ShortcutItem *> &items)
{
    const UniqueIDManager *idmanager = UniqueIDManager::instance();

    QFile file(m_filename);
    if (!file.open(QIODevice::WriteOnly|QIODevice::Text))
        return false;

    const Context ctx;
    QXmlStreamWriter w(&file);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(1); // Historical, used to be QDom.
    w.writeStartDocument();
    w.writeDTD(QLatin1String("<!DOCTYPE KeyboardMappingScheme>"));
    w.writeComment(QString::fromAscii(" Written by Qt Creator %1, %2. ").
                   arg(QLatin1String(Core::Constants::IDE_VERSION_LONG),
                       QDateTime::currentDateTime().toString(Qt::ISODate)));
    w.writeStartElement(ctx.mappingElement);
    foreach (const ShortcutItem *item, items) {
        const QString id = idmanager->stringForUniqueIdentifier(item->m_cmd->id());
        if (item->m_key.isEmpty()) {
            w.writeEmptyElement(ctx.shortCutElement);
            w.writeAttribute(ctx.idAttribute, id);
        } else {
            w.writeStartElement(ctx.shortCutElement);
            w.writeAttribute(ctx.idAttribute, id);
            w.writeEmptyElement(ctx.keyElement);
            w.writeAttribute(ctx.valueAttribute, item->m_key.toString());
            w.writeEndElement(); // Shortcut
        }
    }
    w.writeEndElement();
    w.writeEndDocument();
    file.close();
    return true;
}

} // namespace Internal
} // namespace Core
