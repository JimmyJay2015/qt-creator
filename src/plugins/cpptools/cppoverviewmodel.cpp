/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "cppoverviewmodel.h"

#include <cplusplus/Icons.h>
#include <cplusplus/Literals.h>
#include <cplusplus/Overview.h>
#include <cplusplus/Scope.h>
#include <cplusplus/Symbols.h>

#include <utils/linecolumn.h>
#include <utils/link.h>

using namespace CPlusPlus;
namespace CppTools {

bool OverviewModel::hasDocument() const
{
    return _cppDocument;
}

unsigned OverviewModel::globalSymbolCount() const
{
    unsigned count = 0;
    if (_cppDocument)
        count += _cppDocument->globalSymbolCount();
    return count;
}

Symbol *OverviewModel::globalSymbolAt(unsigned index) const
{ return _cppDocument->globalSymbolAt(index); }

QModelIndex OverviewModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        if (row == 0) // account for no symbol item
            return createIndex(row, column);
        Symbol *symbol = globalSymbolAt(static_cast<unsigned>(row-1)); // account for no symbol item
        return createIndex(row, column, symbol);
    } else {
        Symbol *parentSymbol = static_cast<Symbol *>(
                    parent.internalPointer());
        Q_ASSERT(parentSymbol);

        if (Template *t = parentSymbol->asTemplate())
            if (Symbol *templateParentSymbol = t->declaration())
                parentSymbol = templateParentSymbol;

        Scope *scope = parentSymbol->asScope();
        Q_ASSERT(scope != nullptr);
        return createIndex(row, 0, scope->memberAt(static_cast<unsigned>(row)));
    }
}

QModelIndex OverviewModel::parent(const QModelIndex &child) const
{
    Symbol *symbol = static_cast<Symbol *>(child.internalPointer());
    if (!symbol) // account for no symbol item
        return QModelIndex();

    if (Scope *scope = symbol->enclosingScope()) {
        if (scope->isTemplate() && scope->enclosingScope())
            scope = scope->enclosingScope();
        if (scope->enclosingScope()) {
            QModelIndex index;
            if (scope->enclosingScope() && scope->enclosingScope()->enclosingScope()) // the parent doesn't have a parent
                index = createIndex(static_cast<int>(scope->index()), 0, scope);
            else //+1 to account for no symbol item
                index = createIndex(static_cast<int>(scope->index() + 1), 0, scope);
            return index;
        }
    }

    return QModelIndex();
}

int OverviewModel::rowCount(const QModelIndex &parent) const
{
    if (hasDocument()) {
        if (!parent.isValid()) {
            return static_cast<int>(globalSymbolCount() + 1); // account for no symbol item
        } else {
            if (!parent.parent().isValid() && parent.row() == 0) // account for no symbol item
                return 0;
            Symbol *parentSymbol = static_cast<Symbol *>(
                        parent.internalPointer());
            Q_ASSERT(parentSymbol);

            if (Template *t = parentSymbol->asTemplate())
                if (Symbol *templateParentSymbol = t->declaration())
                    parentSymbol = templateParentSymbol;

            if (Scope *parentScope = parentSymbol->asScope()) {
                if (!parentScope->isFunction() && !parentScope->isObjCMethod())
                    return static_cast<int>(parentScope->memberCount());
            }
            return 0;
        }
    }
    if (!parent.isValid())
        return 1; // account for no symbol item
    return 0;
}

int OverviewModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant OverviewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    // account for no symbol item
    if (!index.parent().isValid() && index.row() == 0) {
        switch (role) {
        case Qt::DisplayRole:
            if (rowCount() > 1)
                return tr("<Select Symbol>");
            else
                return tr("<No Symbols>");
        default:
            return QVariant();
        } //switch
    }

    switch (role) {
    case Qt::DisplayRole: {
        Symbol *symbol = static_cast<Symbol *>(index.internalPointer());
        QString name = _overview.prettyName(symbol->name());
        if (name.isEmpty())
            name = QLatin1String("anonymous");
        if (symbol->isObjCForwardClassDeclaration())
            name = QLatin1String("@class ") + name;
        if (symbol->isObjCForwardProtocolDeclaration() || symbol->isObjCProtocol())
            name = QLatin1String("@protocol ") + name;
        if (symbol->isObjCClass()) {
            ObjCClass *clazz = symbol->asObjCClass();
            if (clazz->isInterface())
                name = QLatin1String("@interface ") + name;
            else
                name = QLatin1String("@implementation ") + name;

            if (clazz->isCategory()) {
                name += QLatin1String(" (") + _overview.prettyName(clazz->categoryName())
                        + QLatin1Char(')');
            }
        }
        if (symbol->isObjCPropertyDeclaration())
            name = QLatin1String("@property ") + name;
        if (Template *t = symbol->asTemplate())
            if (Symbol *templateDeclaration = t->declaration()) {
                QStringList parameters;
                parameters.reserve(static_cast<int>(t->templateParameterCount()));
                for (unsigned i = 0; i < t->templateParameterCount(); ++i)
                    parameters.append(_overview.prettyName(t->templateParameterAt(i)->name()));
                name += QLatin1Char('<') + parameters.join(QLatin1String(", ")) + QLatin1Char('>');
                symbol = templateDeclaration;
            }
        if (symbol->isObjCMethod()) {
            ObjCMethod *method = symbol->asObjCMethod();
            if (method->isStatic())
                name = QLatin1Char('+') + name;
            else
                name = QLatin1Char('-') + name;
        } else if (! symbol->isScope() || symbol->isFunction()) {
            QString type = _overview.prettyType(symbol->type());
            if (Function *f = symbol->type()->asFunctionType()) {
                name += type;
                type = _overview.prettyType(f->returnType());
            }
            if (! type.isEmpty())
                name += QLatin1String(": ") + type;
        }
        return name;
    }

    case Qt::EditRole: {
        Symbol *symbol = static_cast<Symbol *>(index.internalPointer());
        QString name = _overview.prettyName(symbol->name());
        if (name.isEmpty())
            name = QLatin1String("anonymous");
        return name;
    }

    case Qt::DecorationRole: {
        Symbol *symbol = static_cast<Symbol *>(index.internalPointer());
        return Icons::iconForSymbol(symbol);
    }

    case FileNameRole: {
        Symbol *symbol = static_cast<Symbol *>(index.internalPointer());
        return QString::fromUtf8(symbol->fileName(), static_cast<int>(symbol->fileNameLength()));
    }

    case LineNumberRole: {
        Symbol *symbol = static_cast<Symbol *>(index.internalPointer());
        return symbol->line();
    }

    default:
        return QVariant();
    } // switch
}

Symbol *OverviewModel::symbolFromIndex(const QModelIndex &index) const
{
    return static_cast<Symbol *>(index.internalPointer());
}

void OverviewModel::rebuild(Document::Ptr doc)
{
    beginResetModel();
    _cppDocument = doc;
    endResetModel();
}

bool OverviewModel::isGenerated(const QModelIndex &sourceIndex) const
{
    CPlusPlus::Symbol *symbol = symbolFromIndex(sourceIndex);
    return symbol && symbol->isGenerated();
}

Utils::Link OverviewModel::linkFromIndex(const QModelIndex &sourceIndex) const
{
    CPlusPlus::Symbol *symbol = symbolFromIndex(sourceIndex);
    if (!symbol)
        return {};

    return symbol->toLink();
}

Utils::LineColumn OverviewModel::lineColumnFromIndex(const QModelIndex &sourceIndex) const
{
    Utils::LineColumn lineColumn;
    CPlusPlus::Symbol *symbol = symbolFromIndex(sourceIndex);
    if (!symbol)
        return lineColumn;
    lineColumn.line = static_cast<int>(symbol->line());
    lineColumn.column = static_cast<int>(symbol->column());
    return lineColumn;
}

} // namespace CppTools
