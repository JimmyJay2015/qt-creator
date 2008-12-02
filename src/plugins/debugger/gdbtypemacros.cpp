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
#include "gdboptionpage.h"
#include "gdbengine.h"
#include "imports.h"

#include <extensionsystem/pluginmanager.h>
#include <coreplugin/icore.h>

#include <QtCore/QSettings>
#include <QtCore/QByteArray>
#include <QtGui/QFileDialog>

using namespace Debugger::Internal;

TypeMacroPage::TypeMacroPage(GdbSettings *settings)
{
    m_pm = ExtensionSystem::PluginManager::instance();
    m_settings = settings;

    Core::ICore *coreIFace = m_pm->getObject<Core::ICore>();
    if (!coreIFace || !coreIFace->settings())
        return;

    QSettings *s = coreIFace->settings();
    s->beginGroup("GdbOptions");
    if (!s->contains("ScriptFile") && !s->contains("TypeMacros")) {
        //insert qt4 defaults
        m_settings->m_scriptFile = coreIFace->resourcePath() +
            QLatin1String("/gdb/qt4macros");
        for (int i=0; i<3; ++i) {
            QByteArray data;
            QDataStream stream(&data, QIODevice::WriteOnly);
            switch(i) {
            case 0:
                stream << QString("printqstring") << (int)1;
                m_settings->m_typeMacros.insert(QLatin1String("QString"), data);
                break;
            case 1:
                stream << QString("printqcolor") << (int)0;
                m_settings->m_typeMacros.insert(QLatin1String("QColor"), data);
                break;
            case 2:
                stream << QString("printqfont") << (int)1;
                m_settings->m_typeMacros.insert(QLatin1String("QFont"), data);
                break;
            }
        }

        s->setValue("ScriptFile", m_settings->m_scriptFile);
        s->setValue("TypeMacros", m_settings->m_typeMacros);
    } else {
        m_settings->m_scriptFile = s->value("ScriptFile", QString()).toString();
        m_settings->m_typeMacros = s->value("TypeMacros", QMap<QString,QVariant>()).toMap();
    }
    s->endGroup();
}

QString TypeMacroPage::name() const
{
    return tr("Type Macros");
}

QString TypeMacroPage::category() const
{
    return "Debugger|Gdb";
}

QString TypeMacroPage::trCategory() const
{
    return tr("Debugger|Gdb");
}

QWidget *TypeMacroPage::createPage(QWidget *parent)
{
    QString macro;
    int index;

    m_widget = new QWidget(parent);
    m_ui.setupUi(m_widget);

    connect(m_ui.addButton, SIGNAL(clicked()),
        this, SLOT(onAddButton()));

    connect(m_ui.delButton, SIGNAL(clicked()),
        this, SLOT(onDelButton()));

    connect(m_ui.scriptButton, SIGNAL(clicked()),
        this, SLOT(onScriptButton()));

    connect(m_ui.treeWidget, SIGNAL(currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *)),
        this, SLOT(currentItemChanged(QTreeWidgetItem *)));

    connect(m_ui.typeEdit, SIGNAL(textChanged(const QString &)),
        this, SLOT(updateButtonState()));

    connect(m_ui.macroEdit, SIGNAL(textChanged(const QString &)),
        this, SLOT(updateButtonState()));

    QMap<QString, QVariant>::const_iterator i = m_settings->m_typeMacros.constBegin();
    while (i != m_settings->m_typeMacros.constEnd()) {
        QTreeWidgetItem *item = new QTreeWidgetItem(m_ui.treeWidget);
        QDataStream stream(i.value().toByteArray());
        stream >> macro >> index;
        item->setText(0, i.key());
        item->setText(1, macro);
        item->setData(0, Qt::UserRole, index);
        ++i;
    }

    m_ui.scriptEdit->setText(m_settings->m_scriptFile);

    updateButtonState();

    return m_widget;
}

void TypeMacroPage::finished(bool accepted)
{
    if (!accepted)
	return;

    m_settings->m_typeMacros.clear();
    m_settings->m_scriptFile = m_ui.scriptEdit->text();

    for (int i=0; i<m_ui.treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_ui.treeWidget->topLevelItem(i);
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << item->text(1) << item->data(0, Qt::UserRole).toInt();
        m_settings->m_typeMacros.insert(item->text(0), data);
    }

    Core::ICore *coreIFace = m_pm->getObject<Core::ICore>();
    if (coreIFace && coreIFace->settings()) {
        QSettings *s = coreIFace->settings();
        s->beginGroup("GdbOptions");
        s->setValue("ScriptFile", m_settings->m_scriptFile);
        s->setValue("TypeMacros", m_settings->m_typeMacros);
        s->endGroup();
    }
}

void TypeMacroPage::onScriptButton()
{
    QString fileName = QFileDialog::getOpenFileName(m_widget, tr("Select Gdb Script"));
    m_ui.scriptEdit->setText(fileName);
    updateButtonState();
}

void TypeMacroPage::onAddButton()
{
    if (m_ui.typeEdit->text().isEmpty() || m_ui.macroEdit->text().isEmpty())
        return;

    QTreeWidgetItem *item = new QTreeWidgetItem(m_ui.treeWidget);
    item->setText(0, m_ui.typeEdit->text());
    item->setText(1, m_ui.macroEdit->text());
    item->setData(0, Qt::UserRole, m_ui.parseAsBox->currentIndex());

    updateButtonState();
}

void TypeMacroPage::onDelButton()
{
    if (QTreeWidgetItem *item = m_ui.treeWidget->currentItem())
        delete item;
    updateButtonState();
}

void TypeMacroPage::currentItemChanged(QTreeWidgetItem *item)
{
    m_ui.typeEdit->setText(item ? item->text(0) : QString());
    m_ui.macroEdit->setText(item ? item->text(1) : QString());
    m_ui.parseAsBox->setCurrentIndex(item ? item->data(0, Qt::UserRole).toInt() : 0);
    updateButtonState();
}

void TypeMacroPage::updateButtonState()
{
    m_ui.delButton->setEnabled(m_ui.treeWidget->currentItem() != 0);
    m_ui.addButton->setDisabled(m_ui.typeEdit->text().isEmpty()
        || m_ui.macroEdit->text().isEmpty());
}
