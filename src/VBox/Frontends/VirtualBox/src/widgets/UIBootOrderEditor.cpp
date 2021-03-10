/* $Id: UIBootOrderEditor.cpp $ */
/** @file
 * VBox Qt GUI - UIBootListWidget class implementation.
 */

/*
 * Copyright (C) 2009-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Qt includes: */
#include <QGridLayout>
#include <QLabel>
#include <QScrollBar>

/* GUI includes: */
#include "UIBootOrderEditor.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIIconPool.h"
#include "UIToolBar.h"

/* COM includes: */
#include "COMEnums.h"
#include "CMachine.h"
#include "CSystemProperties.h"


/** QListWidgetItem extension for our UIBootListWidget. */
class UIBootListWidgetItem : public QListWidgetItem
{
public:

    /** Constructs boot-table item of passed @a enmType. */
    UIBootListWidgetItem(KDeviceType enmType);

    /** Returns the item type. */
    KDeviceType type() const;

    /** Performs item translation. */
    void retranslateUi();

private:

    /** Holds the item type. */
    KDeviceType m_enmType;
};


/** QListWidget subclass used as system settings boot-table. */
class UIBootListWidget : public QIWithRetranslateUI<QListWidget>
{
    Q_OBJECT;

signals:

    /** Notifies listeners about current table row changed.
      * @note  Same as base-class currentRowChanged but in wider cases. */
    void sigRowChanged(int iRow);

public:

    /** Constructs boot-table passing @a pParent to the base-class. */
    UIBootListWidget(QWidget *pParent = 0);

    /** Defines @a bootItems list. */
    void setBootItems(const UIBootItemDataList &bootItems);
    /** Returns boot item list. */
    UIBootItemDataList bootItems() const;

public slots:

    /** Moves current item up. */
    void sltMoveItemUp();
    /** Moves current item down. */
    void sltMoveItemDown();

protected:

    /** Handles translation event. */
    virtual void retranslateUi() /* override */;

    /** Handles drop @a pEvent. */
    virtual void dropEvent(QDropEvent *pEvent) /* override */;

    /** Returns a QModelIndex object pointing to the next object in the view,
      * based on the given @a cursorAction and keyboard @a fModifiers. */
    virtual QModelIndex moveCursor(QAbstractItemView::CursorAction cursorAction,
                                   Qt::KeyboardModifiers fModifiers) /* override */;

private:

    /** Prepares all. */
    void prepare();

    /** Adjusts table size to fit contents. */
    void adjustSizeToFitContent();

    /** Moves item with passed @a index to specified @a iRow. */
    QModelIndex moveItemTo(const QModelIndex &index, int iRow);
};


/*********************************************************************************************************************************
*   Class UIBootListWidgetItem implementation.                                                                                   *
*********************************************************************************************************************************/

UIBootListWidgetItem::UIBootListWidgetItem(KDeviceType enmType)
    : m_enmType(enmType)
{
    setCheckState(Qt::Unchecked);
    switch(enmType)
    {
        case KDeviceType_Floppy:   setIcon(UIIconPool::iconSet(":/fd_16px.png")); break;
        case KDeviceType_DVD:      setIcon(UIIconPool::iconSet(":/cd_16px.png")); break;
        case KDeviceType_HardDisk: setIcon(UIIconPool::iconSet(":/hd_16px.png")); break;
        case KDeviceType_Network:  setIcon(UIIconPool::iconSet(":/nw_16px.png")); break;
        default: break; /* Shut up, MSC! */
    }
    retranslateUi();
}

KDeviceType UIBootListWidgetItem::type() const
{
    return m_enmType;
}

void UIBootListWidgetItem::retranslateUi()
{
    setText(gpConverter->toString(m_enmType));
}


/*********************************************************************************************************************************
*   Class UIBootListWidget implementation.                                                                                       *
*********************************************************************************************************************************/

UIBootListWidget::UIBootListWidget(QWidget *pParent /* = 0 */)
    : QIWithRetranslateUI<QListWidget>(pParent)
{
    prepare();
}

void UIBootListWidget::setBootItems(const UIBootItemDataList &bootItems)
{
    /* Clear initially: */
    clear();

    /* Apply internal variables data to QWidget(s): */
    foreach (const UIBootItemData &data, bootItems)
    {
        UIBootListWidgetItem *pItem = new UIBootListWidgetItem(data.m_enmType);
        pItem->setCheckState(data.m_fEnabled ? Qt::Checked : Qt::Unchecked);
        addItem(pItem);
    }

    /* Adjust table size after change: */
    adjustSizeToFitContent();
}

UIBootItemDataList UIBootListWidget::bootItems() const
{
    /* Prepare boot items: */
    UIBootItemDataList bootItems;

    /* Enumerate all the items we have: */
    for (int i = 0; i < count(); ++i)
    {
        QListWidgetItem *pItem = item(i);
        UIBootItemData bootData;
        bootData.m_enmType = static_cast<UIBootListWidgetItem*>(pItem)->type();
        bootData.m_fEnabled = pItem->checkState() == Qt::Checked;
        bootItems << bootData;
    }

    /* Return boot items: */
    return bootItems;
}

void UIBootListWidget::sltMoveItemUp()
{
    QModelIndex index = currentIndex();
    moveItemTo(index, index.row() - 1);
}

void UIBootListWidget::sltMoveItemDown()
{
    QModelIndex index = currentIndex();
    moveItemTo(index, index.row() + 2);
}

void UIBootListWidget::retranslateUi()
{
    for (int i = 0; i < count(); ++i)
        static_cast<UIBootListWidgetItem*>(item(i))->retranslateUi();

    adjustSizeToFitContent();
}

void UIBootListWidget::dropEvent(QDropEvent *pEvent)
{
    /* Call to base-class: */
    QListWidget::dropEvent(pEvent);
    /* Separately notify listeners: */
    emit sigRowChanged(currentRow());
}

QModelIndex UIBootListWidget::moveCursor(QAbstractItemView::CursorAction cursorAction, Qt::KeyboardModifiers fModifiers)
{
    if (fModifiers.testFlag(Qt::ControlModifier))
    {
        switch (cursorAction)
        {
            case QAbstractItemView::MoveUp:
            {
                QModelIndex index = currentIndex();
                return moveItemTo(index, index.row() - 1);
            }
            case QAbstractItemView::MoveDown:
            {
                QModelIndex index = currentIndex();
                return moveItemTo(index, index.row() + 2);
            }
            case QAbstractItemView::MovePageUp:
            {
                QModelIndex index = currentIndex();
                return moveItemTo(index, qMax(0, index.row() - verticalScrollBar()->pageStep()));
            }
            case QAbstractItemView::MovePageDown:
            {
                QModelIndex index = currentIndex();
                return moveItemTo(index, qMin(model()->rowCount(), index.row() + verticalScrollBar()->pageStep() + 1));
            }
            case QAbstractItemView::MoveHome:
                return moveItemTo(currentIndex(), 0);
            case QAbstractItemView::MoveEnd:
                return moveItemTo(currentIndex(), model()->rowCount());
            default:
                break;
        }
    }
    return QListWidget::moveCursor(cursorAction, fModifiers);
}

void UIBootListWidget::prepare()
{
    setDragDropMode(QAbstractItemView::InternalMove);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDropIndicatorShown(true);
    setUniformItemSizes(true);
    connect(this, &UIBootListWidget::currentRowChanged,
            this, &UIBootListWidget::sigRowChanged);
}

void UIBootListWidget::adjustSizeToFitContent()
{
    const int iH = 2 * frameWidth();
    const int iW = iH;
    setFixedSize(sizeHintForColumn(0) + iW,
                 sizeHintForRow(0) * count() + iH);
}

QModelIndex UIBootListWidget::moveItemTo(const QModelIndex &index, int row)
{
    /* Check validity: */
    if (!index.isValid())
        return QModelIndex();

    /* Check sanity: */
    if (row < 0 || row > model()->rowCount())
        return QModelIndex();

    QPersistentModelIndex oldIndex(index);
    UIBootListWidgetItem *pItem = static_cast<UIBootListWidgetItem*>(itemFromIndex(oldIndex));
    insertItem(row, new UIBootListWidgetItem(*pItem));
    QPersistentModelIndex newIndex = model()->index(row, 0);
    delete takeItem(oldIndex.row());
    setCurrentRow(newIndex.row());
    return QModelIndex(newIndex);
}


/*********************************************************************************************************************************
*   Class UIBootDataTools implementation.                                                                                        *
*********************************************************************************************************************************/

UIBootItemDataList UIBootDataTools::loadBootItems(const CMachine &comMachine)
{
    /* Gather a list of all possible boot items.
     * Currently, it seems, we are supporting only 4 possible boot device types:
     * 1. Floppy, 2. DVD-ROM, 3. Hard Disk, 4. Network.
     * But maximum boot devices count supported by machine should be retrieved
     * through the ISystemProperties getter.  Moreover, possible boot device
     * types are not listed in some separate Main vector, so we should get them
     * (randomely?) from the list of all device types.  Until there will be a
     * separate Main getter for list of supported boot device types, this list
     * will be hard-coded here... */
    QVector<KDeviceType> possibleBootItems = QVector<KDeviceType>() << KDeviceType_Floppy
                                                                    << KDeviceType_DVD
                                                                    << KDeviceType_HardDisk
                                                                    << KDeviceType_Network;
    const CSystemProperties comProperties = uiCommon().virtualBox().GetSystemProperties();
    const int iPossibleBootListSize = qMin((ULONG)4, comProperties.GetMaxBootPosition());
    possibleBootItems.resize(iPossibleBootListSize);

    /* Prepare boot items: */
    UIBootItemDataList bootItems;

    /* Gather boot-items of current VM: */
    QList<KDeviceType> usedBootItems;
    for (int i = 1; i <= possibleBootItems.size(); ++i)
    {
        const KDeviceType enmType = comMachine.GetBootOrder(i);
        if (enmType != KDeviceType_Null)
        {
            usedBootItems << enmType;
            UIBootItemData data;
            data.m_enmType = enmType;
            data.m_fEnabled = true;
            bootItems << data;
        }
    }
    /* Gather other unique boot-items: */
    for (int i = 0; i < possibleBootItems.size(); ++i)
    {
        const KDeviceType enmType = possibleBootItems.at(i);
        if (!usedBootItems.contains(enmType))
        {
            UIBootItemData data;
            data.m_enmType = enmType;
            data.m_fEnabled = false;
            bootItems << data;
        }
    }

    /* Return boot items: */
    return bootItems;
}

void UIBootDataTools::saveBootItems(const UIBootItemDataList &bootItems, CMachine &comMachine)
{
    bool fSuccess = true;
    int iBootIndex = 0;
    for (int i = 0; fSuccess && i < bootItems.size(); ++i)
    {
        if (bootItems.at(i).m_fEnabled)
        {
            comMachine.SetBootOrder(++iBootIndex, bootItems.at(i).m_enmType);
            fSuccess = comMachine.isOk();
        }
    }
    for (int i = 0; fSuccess && i < bootItems.size(); ++i)
    {
        if (!bootItems.at(i).m_fEnabled)
        {
            comMachine.SetBootOrder(++iBootIndex, KDeviceType_Null);
            fSuccess = comMachine.isOk();
        }
    }
}

QString UIBootDataTools::bootItemsToReadableString(const UIBootItemDataList &bootItems)
{
    /* Prepare list: */
    QStringList list;
    /* We are reflecting only enabled items: */
    foreach (const UIBootItemData &bootItem, bootItems)
        if (bootItem.m_fEnabled)
            list << gpConverter->toString(bootItem.m_enmType);
    /* But if list is empty we are adding Null item at least: */
    if (list.isEmpty())
        list << gpConverter->toString(KDeviceType_Null);
    /* Join list to string: */
    return list.join(", ");
}

QString UIBootDataTools::bootItemsToSerializedString(const UIBootItemDataList &bootItems)
{
    /* Prepare list: */
    QStringList list;
    /* This is simple, we are adding '+' before enabled types and '-' before disabled: */
    foreach (const UIBootItemData &bootItem, bootItems)
        list << (bootItem.m_fEnabled ? QString("+%1").arg(bootItem.m_enmType) : QString("-%1").arg(bootItem.m_enmType));
    /* Join list to string: */
    return list.join(';');
}

UIBootItemDataList UIBootDataTools::bootItemsFromSerializedString(const QString &strBootItems)
{
    /* Prepare list: */
    UIBootItemDataList list;
    /* First of all, split passed string to arguments: */
    const QStringList arguments = strBootItems.split(';');
    /* Now parse in backward direction, we have added '+' before enabled types and '-' before disabled: */
    foreach (QString strArgument, arguments)
    {
        UIBootItemData data;
        data.m_fEnabled = strArgument.startsWith('+');
        strArgument.remove(QRegExp("[+-]"));
        data.m_enmType = static_cast<KDeviceType>(strArgument.toInt());
        list << data;
    }
    /* Return list: */
    return list;
}


/*********************************************************************************************************************************
*   Class UIBootOrderEditor implementation.                                                                                      *
*********************************************************************************************************************************/

UIBootOrderEditor::UIBootOrderEditor(QWidget *pParent /* = 0 */, bool fWithLabel /* = false */)
    : QIWithRetranslateUI<QWidget>(pParent)
    , m_fWithLabel(fWithLabel)
    , m_pLabel(0)
    , m_pTable(0)
    , m_pToolbar(0)
    , m_pMoveUp(0)
    , m_pMoveDown(0)
{
    prepare();
}

void UIBootOrderEditor::setValue(const UIBootItemDataList &guiValue)
{
    if (m_pTable)
        m_pTable->setBootItems(guiValue);
}

UIBootItemDataList UIBootOrderEditor::value() const
{
    return m_pTable ? m_pTable->bootItems() : UIBootItemDataList();
}

bool UIBootOrderEditor::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Skip events sent to unrelated objects: */
    if (m_pTable && pObject != m_pTable)
        return QIWithRetranslateUI<QWidget>::eventFilter(pObject, pEvent);

    /* Handle only required event types: */
    switch (pEvent->type())
    {
        case QEvent::FocusIn:
        case QEvent::FocusOut:
        {
            /* On focus in/out events we'd like
             * to update actions availability: */
            updateActionAvailability();
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QIWithRetranslateUI<QWidget>::eventFilter(pObject, pEvent);
}

void UIBootOrderEditor::retranslateUi()
{
    if (m_pLabel)
        m_pLabel->setText(tr("&Boot Order:"));
    if (m_pMoveUp)
        m_pMoveUp->setText(tr("Move Up"));
    if (m_pMoveDown)
        m_pMoveDown->setText(tr("Move Down"));
}

void UIBootOrderEditor::sltHandleCurrentBootItemChange()
{
    /* On current item change signals we'd like
     * to update actions availability: */
    updateActionAvailability();
}

void UIBootOrderEditor::prepare()
{
    /* Create main layout: */
    QGridLayout *pMainLayout = new QGridLayout(this);
    if (pMainLayout)
    {
        pMainLayout->setContentsMargins(0, 0, 0, 0);
        int iRow = 0;

        /* Create label: */
        if (m_fWithLabel)
            m_pLabel = new QLabel(this);
        if (m_pLabel)
            pMainLayout->addWidget(m_pLabel, 0, iRow++, 1, 1);

        /* Create table layout: */
        QHBoxLayout *pTableLayout = new QHBoxLayout;
        if (pTableLayout)
        {
            pTableLayout->setContentsMargins(0, 0, 0, 0);
            pTableLayout->setSpacing(1);

            /* Create table: */
            m_pTable = new UIBootListWidget(this);
            if (m_pTable)
            {
                setFocusProxy(m_pTable);
                if (m_pLabel)
                    m_pLabel->setBuddy(m_pTable);
                m_pTable->setAlternatingRowColors(true);
                m_pTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                m_pTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                connect(m_pTable, &UIBootListWidget::sigRowChanged,
                        this, &UIBootOrderEditor::sltHandleCurrentBootItemChange);
                pTableLayout->addWidget(m_pTable);
            }

            /* Create tool-bar: */
            m_pToolbar = new UIToolBar(this);
            if (m_pToolbar)
            {
                m_pToolbar->setIconSize(QSize(16, 16));
                m_pToolbar->setOrientation(Qt::Vertical);

                /* Create Up action: */
                m_pMoveUp = m_pToolbar->addAction(UIIconPool::iconSet(":/list_moveup_16px.png",
                                                                      ":/list_moveup_disabled_16px.png"),
                                                  QString(), m_pTable, &UIBootListWidget::sltMoveItemUp);
                /* Create Down action: */
                m_pMoveDown = m_pToolbar->addAction(UIIconPool::iconSet(":/list_movedown_16px.png",
                                                                        ":/list_movedown_disabled_16px.png"),
                                                    QString(), m_pTable, &UIBootListWidget::sltMoveItemDown);

                /* Add tool-bar into table layout: */
                pTableLayout->addWidget(m_pToolbar);
            }

            /* Add table layout to main layout: */
            pMainLayout->addLayout(pTableLayout, 0, iRow++, 3, 1);
        }
    }

    /* Update initial action availability: */
    updateActionAvailability();
    /* Apply language settings: */
    retranslateUi();
}

void UIBootOrderEditor::updateActionAvailability()
{
    /* Update move up/down actions: */
    if (m_pTable && m_pMoveUp)
        m_pMoveUp->setEnabled(m_pTable->hasFocus() && m_pTable->currentRow() > 0);
    if (m_pTable && m_pMoveDown)
        m_pMoveDown->setEnabled(m_pTable->hasFocus() && m_pTable->currentRow() < m_pTable->count() - 1);
}

#include "UIBootOrderEditor.moc"
