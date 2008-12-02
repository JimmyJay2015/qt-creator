/***************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2008 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact:  Qt Software Information (qt-info@nokia.com)
**
**
** Non-Open Source Usage
**
** Licensees may use this file in accordance with the Qt Beta Version
** License Agreement, Agreement version 2.2 provided with the Software or,
** alternatively, in accordance with the terms contained in a written
** agreement between you and Nokia.
**
** GNU General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU General
** Public License versions 2.0 or 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the packaging
** of this file.  Please review the following information to ensure GNU
** General Public Licensing requirements will be met:
**
** http://www.fsf.org/licensing/licenses/info/GPLv2.html and
** http://www.gnu.org/copyleft/gpl.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt GPL Exception
** version 1.2, included in the file GPL_EXCEPTION.txt in this package.
**
***************************************************************************/
#ifndef _TEXTBLOCKITERATOR_H
#define _TEXTBLOCKITERATOR_H

#include "texteditor_global.h"

#include <QtGui/QTextBlock>

namespace TextEditor {

/* Iterator for the text blocks of a document. */
class TEXTEDITOR_EXPORT TextBlockIterator {
public:
    TextBlockIterator();
    TextBlockIterator(const QTextBlock &block);

    bool equals(const TextBlockIterator &o) const;

    QString operator*() const;
    TextBlockIterator &operator++();
    TextBlockIterator &operator--();
    TextBlockIterator operator++(int);
    TextBlockIterator operator--(int);

private:
    void read() const;

private:
    const QTextDocument *m_document;
    QTextBlock m_block;
    mutable QString m_text;
    mutable bool m_initialized;
};

inline bool operator==(const TextBlockIterator &i1, const TextBlockIterator &i2) { return i1.equals(i2); }
inline bool operator!=(const TextBlockIterator &i1, const TextBlockIterator &i2) { return !i1.equals(i2); }

} // namespace TextEditor

#endif // _TEXTBLOCKITERATOR_H
