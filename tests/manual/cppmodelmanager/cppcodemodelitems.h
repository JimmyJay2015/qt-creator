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
/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

/* This file is part of KDevelop
Copyright (C) 2002-2005 Roberto Raggi <roberto@kdevelop.org>
Copyright (C) 2005 Trolltech AS

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License version 2 as published by the Free Software Foundation.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public License
along with this library; see the file COPYING.LIB.  If not, write to
the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
Boston, MA 02110-1301, USA.
*/

#ifndef CPPCODEMODELITEMS_H
#define CPPCODEMODELITEMS_H

class CppCodeModel;
class CppCodeModelPart;
#include "codemodel/codemodelitems.h"

class CppClassModelItem: public ClassModelItem
{
public:
    CppClassModelItem(CppCodeModel *model);
};

class CppFileModelItem: public FileModelItem
{
public:
    CppFileModelItem(CppCodeModel *model);
    ~CppFileModelItem();

    void setPart(CppCodeModelPart *part);
    CppCodeModelPart *part() const;
    
    ScopeModelItem *findExternalScope(const QString &name) const;
    void addExternalScope(ScopeModelItem *item);
    QList<ScopeModelItem *> externalScopes() const;

private:
    QHash<QString, ScopeModelItem *> _M_externalscopes;
    CppCodeModelPart *_M_part;
};
DECLARE_LANGUAGE_MODELITEM(File, Cpp)

class CppArgumentModelItem: public ArgumentModelItem
{
public:
    CppArgumentModelItem(CppCodeModel *model);
};
DECLARE_LANGUAGE_MODELITEM(Argument, Cpp)

class CppFunctionDefinitionModelItem : public FunctionDefinitionModelItem
{
public:
    CppFunctionDefinitionModelItem(CppCodeModel *model);
};
DECLARE_LANGUAGE_MODELITEM(FunctionDefinition, Cpp)

class CppVariableModelItem : public VariableModelItem
{
public:
    CppVariableModelItem(CppCodeModel *model);
};
DECLARE_LANGUAGE_MODELITEM(Variable, Cpp)

class CppTypeAliasModelItem : public TypeAliasModelItem
{
public:
    CppTypeAliasModelItem(CppCodeModel *model);
};
DECLARE_LANGUAGE_MODELITEM(TypeAlias, Cpp)

class CppEnumModelItem : public EnumModelItem
{
public:
    CppEnumModelItem(CppCodeModel *model);
};
DECLARE_LANGUAGE_MODELITEM(Enum, Cpp)

class CppEnumeratorModelItem : public EnumeratorModelItem
{
public:
    CppEnumeratorModelItem(CppCodeModel *model);
};
DECLARE_LANGUAGE_MODELITEM(Enumerator, Cpp)

class CppTemplateParameterModelItem : public TemplateParameterModelItem
{
public:
    CppTemplateParameterModelItem(CppCodeModel *model);
    CppTemplateParameterModelItem(const CppTemplateParameterModelItem &item);
};
DECLARE_LANGUAGE_MODELITEM(TemplateParameter, Cpp)

#endif //CPPCODEMODELITEMS_H

// kate: space-indent on; indent-width 2; replace-tabs on;
